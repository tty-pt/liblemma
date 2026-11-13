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
export function foldAccents(text, options = {}) {
  if (!text) return "";
  if (!options.wasm && nativeBinding && nativeBinding.fold) {
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
export function tokenize(text, options = {}) {
  if (!text) return [];
  if (!options.wasm && nativeBinding && nativeBinding.tokenize) {
    return nativeBinding.tokenize(text);
  }
  const normalized = foldAccents(text, options);
  const wasm = getLemmaWasm();
  const substrate = getSubstrate();
  return substrate.withString(normalized, (inPtr) => {
    const outCap = normalized.length * 4 + 128;
    const outPtr = wasm.exports.malloc(outCap);
    try {
      wasm.exports.lemma_wasm_tokenize(inPtr, outPtr, outCap);
      const rawTokens = substrate.readString(outPtr);
      return rawTokens.split("\n").filter(Boolean);
    } finally {
      wasm.exports.free(outPtr);
    }
  });
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
