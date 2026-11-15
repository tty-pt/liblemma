import { loadWasmModule, loadNativeAddon, getSubstrate } from "@tty-pt/stele";

const nativeBinding = loadNativeAddon(import.meta.url, "lemma");
let wasmInstance = null;

export function getLemmaWasm() {
  if (wasmInstance) return wasmInstance;
  wasmInstance = loadWasmModule(new URL("./lemma.wasm", import.meta.url));
  return wasmInstance;
}

/**
 * Folds Unicode case while preserving accents using liblemma's C engine (e.g. "Pão" -> "pão")
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
 * Tokenizes text into word units using liblemma's C engine
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
 * Lossless 16-bit vocabulary token encoding via liblemma C engine
 */
export function encodeTokens(text) {
  if (!nativeBinding || !nativeBinding.tokEncode) {
    throw new Error("@tty-pt/lemma native addon is required for encodeTokens");
  }
  return nativeBinding.tokEncode(String(text ?? ""));
}

/**
 * Lossless 16-bit vocabulary token decoding via liblemma C engine
 */
export function decodeTokens(tokens) {
  if (!nativeBinding || !nativeBinding.tokDecode) {
    throw new Error("@tty-pt/lemma native addon is required for decodeTokens");
  }
  const u16 = tokens instanceof Uint16Array ? tokens : new Uint16Array(tokens);
  return nativeBinding.tokDecode(u16);
}

/**
 * C-backed full-text inverted index (lemma_db_t)
 */
export class LemmaIndex {
  constructor(specOrMask = 0) {
    if (!nativeBinding || !nativeBinding.open) {
      throw new Error("@tty-pt/lemma native addon is required for LemmaIndex");
    }
    this.handle = nativeBinding.open(specOrMask);
  }

  addDocument(id, text, field = "text") {
    if (!this.handle) throw new Error("LemmaIndex is closed");
    return nativeBinding.indexRef(this.handle, field, Number(id), String(text ?? ""));
  }

  removeDocument(id, field = "text") {
    if (!this.handle) throw new Error("LemmaIndex is closed");
    return nativeBinding.unindexRef(this.handle, field, Number(id));
  }

  storeTyped(ref, uint8Array, qtype = 0) {
    if (!this.handle) throw new Error("LemmaIndex is closed");
    return nativeBinding.storeTyped(this.handle, Number(ref), uint8Array, qtype);
  }

  fill(query, options = {}) {
    if (!this.handle) throw new Error("LemmaIndex is closed");
    const field = options.field || "text";
    const phrase = Boolean(options.phrase);
    return nativeBinding.fillTokens(this.handle, field, String(query ?? ""), phrase);
  }

  rank(queryOrMatchedCount, ref, field = "text") {
    if (!this.handle) throw new Error("LemmaIndex is closed");
    return nativeBinding.rank(this.handle, field, queryOrMatchedCount, Number(ref));
  }

  search(query, options = {}) {
    if (!this.handle) throw new Error("LemmaIndex is closed");
    const field = options.field || "text";
    const phrase = Boolean(options.phrase);
    const topK = options.topK || 100;
    return nativeBinding.search(this.handle, field, String(query ?? ""), phrase, topK);
  }

  clear() {
    if (this.handle) {
      nativeBinding.clear(this.handle);
    }
  }

  close() {
    if (this.handle) {
      nativeBinding.close(this.handle);
      this.handle = null;
    }
  }
}

/**
 * Pluggable CormAxis implementation for the Corm Recall Kernel backed by liblemma C engine
 */
export class LemmaLexicalAxis {
  constructor(specOrMask = 0) {
    this.name = "lexical";
    this.index = new LemmaIndex(specOrMask);
  }

  registerText(ref, text, field = "text") {
    this.index.addDocument(ref, text, field);
  }

  unstore(ref, field = "text") {
    return this.index.removeDocument(ref, field);
  }

  storeTyped(ref, uint8Array, qtype = 0) {
    return this.index.storeTyped(ref, uint8Array, qtype);
  }

  fill(params) {
    const query = params?.term ?? params?.query ?? "";
    const field = params?.field ?? "text";
    const phrase = Boolean(params?.phrase);
    return this.index.fill(query, { field, phrase });
  }

  rank(params, ref) {
    const query = params?.term ?? params?.query ?? "";
    const field = params?.field ?? "text";
    return this.index.rank(query, ref, field);
  }

  close() {
    this.index.close();
  }
}
