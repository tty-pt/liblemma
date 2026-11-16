import test from "node:test";
import assert from "node:assert/strict";
import {
  foldAccents,
  tokenize,
  encodeTokens,
  decodeTokens,
  LemmaIndex,
  LemmaLexicalAxis,
} from "../dist/index.js";

test("foldAccents performs Unicode case folding according to liblemma contract", () => {
  assert.equal(foldAccents("Pão"), "pão");
  assert.equal(foldAccents("ÉTÉ"), "été");
  assert.equal(foldAccents("Straße"), "straße");
  assert.equal(foldAccents("Café"), "café");
});

test("tokenize, LemmaIndex search, phrase query, and removeDocument", () => {
  const tokens = tokenize("O Pão na padaria de Évora!");
  assert.ok(tokens.includes("pão"));
  assert.ok(tokens.includes("évora"));
  assert.ok(tokens.includes("padaria"));

  const index = new LemmaIndex();
  index.addDocument(1, "Padaria tradicional com pão fresco");
  index.addDocument(2, "Restaurante de peixe grelhado");
  index.addDocument(3, "Café com pão de queijo");

  const results = index.search("pão");
  assert.equal(results.length, 2);
  assert.ok(results.some((r) => r.id === 1));
  assert.ok(results.some((r) => r.id === 3));

  const phraseHits = index.search("pão fresco", { phrase: true });
  assert.equal(phraseHits.length, 1);
  assert.equal(phraseHits[0].id, 1);

  index.removeDocument(1);
  const afterRemove = index.search("pão");
  assert.equal(afterRemove.length, 1);
  assert.equal(afterRemove[0].id, 3);
  index.close();
});

test("encodeTokens and decodeTokens round-trip losslessly", () => {
  const original = "C systems programming: libcorm, liblemma, libsepal & Café Pão 2026!";
  const encoded = encodeTokens(original);
  assert.ok(encoded instanceof Uint16Array);
  assert.ok(encoded.length > 0);
  const decoded = decodeTokens(encoded);
  assert.equal(decoded, original);
});

test("LemmaLexicalAxis fill and rank", () => {
  const axis = new LemmaLexicalAxis();
  axis.registerText(10, "fast spatial morton indexing");
  axis.registerText(20, "fast spatial morton indexing with extra words in document");

  const filled = axis.fill({ term: "spatial morton" });
  assert.deepEqual(filled, [10, 20]);

  const s10 = axis.rank({ term: "spatial morton" }, 10);
  const s20 = axis.rank({ term: "spatial morton" }, 20);
  assert.ok(s10 > s20, "Shorter matching document should rank higher");

  axis.unstore(10);
  assert.deepEqual(axis.fill({ term: "spatial morton" }), [20]);
  axis.close();
});

test("foldAccents and tokenize execute via pure WebAssembly substrate path ({ wasm: true })", () => {
  assert.equal(foldAccents("Pão", { wasm: true }), "pão");
  assert.equal(foldAccents("ÉTÉ", { wasm: true }), "été");
  assert.equal(foldAccents("Straße", { wasm: true }), "straße");
  assert.equal(foldAccents("Café", { wasm: true }), "café");

  const tokens = tokenize("O Pão na padaria de Évora!", { wasm: true });
  assert.deepEqual(tokens, ["o", "pão", "na", "padaria", "de", "évora"]);
});
