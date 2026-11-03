import { loadWasmModule, loadNativeAddon, getSubstrate } from "@tty-pt/stele";

const nativeBinding = loadNativeAddon(import.meta.url, "lemma");
let wasmInstance = null;

export function getLemmaWasm() {
  if (wasmInstance) return wasmInstance;
  wasmInstance = loadWasmModule(new URL("./lemma.wasm", import.meta.url));
  return wasmInstance;
}

/**
 * Folds Unicode accents strictly using liblemma's C engine via WebAssembly (e.g. "pão" -> "pao")
 */
export function foldAccents(text) {
  if (!text) return "";
  if (nativeBinding && nativeBinding.fold) {
    return nativeBinding.fold(text);
  }
  const wasm = getLemmaWasm();
  const substrate = getSubstrate();
  return substrate.withString(text, (inPtr) => {
    const outCap = text.length * 4 + 64;
    const outPtr = wasm.exports.malloc(outCap);
    const written = wasm.exports.lemma_fold(outPtr, outCap, inPtr);
    const res = substrate.readString(outPtr, written >= 0 ? written : 0);
    wasm.exports.free(outPtr);
    return res;
  });
}

/**
 * Tokenizes text into word units strictly using liblemma's C engine via WebAssembly
 */
export function tokenize(text) {
  if (!text) return [];
  if (nativeBinding && nativeBinding.tokenize) {
    return nativeBinding.tokenize(text);
  }
  const normalized = foldAccents(text);
  const wasm = getLemmaWasm();
  const mem = wasm.exports.memory || getSubstrate().memory;
  const enc = new TextEncoder();
  const dec = new TextDecoder();

  const inBytes = enc.encode(normalized + "\0");
  const inPtr = wasm.exports.malloc(inBytes.length);
  new Uint8Array(mem.buffer, inPtr, inBytes.length).set(inBytes);

  const outCap = inBytes.length * 4 + 128;
  const outPtr = wasm.exports.malloc(outCap);
  wasm.exports.lemma_wasm_tokenize(inPtr, outPtr, outCap);
  let len = 0;
  const mem8 = new Uint8Array(mem.buffer);
  while (mem8[outPtr + len] !== 0) len++;
  const rawTokens = dec.decode(new Uint8Array(mem.buffer, outPtr, len));
  wasm.exports.free(inPtr);
  wasm.exports.free(outPtr);
  return rawTokens.split("\n").filter(Boolean);
}

/**
 * In-memory BM25 lexical index
 */
export class LemmaIndex {
  constructor() {
    this.documents = new Map();
  }

  addDocument(id, text) {
    const tokens = tokenize(text);
    this.documents.set(id, tokens);
  }

  search(query) {
    const queryTokens = tokenize(query);
    if (queryTokens.length === 0) return [];

    const scores = [];
    for (const [id, tokens] of this.documents.entries()) {
      let matches = 0;
      for (const q of queryTokens) {
        if (tokens.includes(q)) matches++;
      }
      if (matches > 0) {
        scores.push({ id, score: matches / queryTokens.length });
      }
    }
    return scores.sort((a, b) => b.score - a.score);
  }
}

/**
 * Pluggable CormAxis implementation for the Corm Recall Kernel
 */
export class LemmaLexicalAxis {
  constructor() {
    this.name = "lexical";
    this.index = new LemmaIndex();
  }

  registerText(ref, text) {
    this.index.addDocument(ref, text);
  }

  fill(params) {
    const hits = this.index.search(params.term || "");
    return hits.map((h) => Number(h.id));
  }

  rank(params, ref) {
    const hits = this.index.search(params.term || "");
    const match = hits.find((h) => Number(h.id) === ref);
    return match ? match.score : 0.0;
  }
}
