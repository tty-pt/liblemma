import { createRequire } from "node:module";
import path from "node:path";

let nativeBinding = null;
try {
  const req = createRequire(import.meta.url);
  nativeBinding = req("../build/Release/lemma_node.node");
} catch {
  try {
    const req = createRequire(import.meta.url);
    nativeBinding = req(path.join(process.cwd(), "build/Release/lemma_node.node"));
  } catch {
    // Native addon not available; JS fallback
  }
}

function getSharedWasm() {
  if (globalThis.__ttypt_shared_wasm__) {
    return globalThis.__ttypt_shared_wasm__;
  }
  try {
    const req = createRequire(import.meta.url);
    const qsysPaths = [
      path.join(path.dirname(import.meta.url.replace("file://", "")), "../../libqsys/npm/dist/index.js"),
      path.join(process.cwd(), "node_modules/@ttypt/qsys/dist/index.js"),
      "@ttypt/qsys"
    ];
    for (const p of qsysPaths) {
      try {
        const mod = req(p);
        if (mod && mod.getSharedWasm) {
          return mod.getSharedWasm();
        }
      } catch {}
    }
  } catch {}
  return null;
}

/**
 * Folds Unicode accents using liblemma's C engine (e.g. "pão" -> "pao")
 */
export function foldAccents(text) {
  if (!text) return "";
  if (nativeBinding && nativeBinding.fold) {
    return nativeBinding.fold(text);
  }
  const wasm = getSharedWasm();
  if (wasm) {
    const inBytes = Buffer.from(text + "\0", "utf8");
    const inPtr = wasm.malloc(inBytes.length);
    new Uint8Array(wasm.memory.buffer).set(inBytes, inPtr);
    const outCap = inBytes.length * 4 + 64;
    const outPtr = wasm.malloc(outCap);
    const written = wasm.lemma_fold(outPtr, outCap, inPtr);
    let res = text;
    if (written >= 0) {
      res = Buffer.from(new Uint8Array(wasm.memory.buffer, outPtr, written)).toString("utf8");
    }
    wasm.free(inPtr);
    wasm.free(outPtr);
    return res;
  }
  // Pure JS fallback
  return text.normalize("NFD").replace(/[\u0300-\u036f]/g, "").toLowerCase();
}

/**
 * Tokenizes text into normalized words using liblemma case-folding
 */
export function tokenize(text) {
  if (!text) return [];
  const normalized = foldAccents(text);
  return normalized
    .replace(/[^\w\s\u00C0-\u017F]/g, " ")
    .split(/\s+/)
    .filter(Boolean);
}

/**
 * Simple in-memory BM25 lexical index
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
