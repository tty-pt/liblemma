import test from "node:test";
import assert from "node:assert/strict";
import { foldAccents, tokenize, LemmaIndex } from "../dist/index.js";

test("foldAccents performs Unicode case folding according to liblemma contract", () => {
  assert.equal(foldAccents("Pão"), "pão");
  assert.equal(foldAccents("ÉTÉ"), "été");
  assert.equal(foldAccents("Straße"), "straße");
  assert.equal(foldAccents("Café"), "café");
});

test("tokenize and LemmaIndex search", () => {
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
});
