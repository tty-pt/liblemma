<p align="center">
  <img src="https://raw.githubusercontent.com/tty-pt/liblemma/main/assets/logo.png" alt="@ttypt/lemma logo" width="240" />
</p>

# @ttypt/lemma

[![npm version](https://img.shields.io/badge/npm-v0.1.0-CB3837?logo=npm)](#)
[![License: BSD-2-Clause](https://img.shields.io/badge/License-BSD--2--Clause-blue)](#)
[![TypeScript Ready](https://img.shields.io/badge/TypeScript-Ready-3178C6?logo=typescript)](#)
[![Node.js Native](https://img.shields.io/badge/Node.js-N--API-339933?logo=node.js)](#)
[![WebAssembly](https://img.shields.io/badge/WebAssembly-Reactor-654FF0?logo=webassembly)](#)
[![Zero Dependencies](https://img.shields.io/badge/dependencies-0-success)](#)

> Universal Node.js and Browser bindings for **`liblemma`**, the pure-C lexical tokenization, Unicode case-folding, and full-text search engine. Represents the canonical **Text Axis** in the Corm Recall Kernel.

---

## 1. Quickstart

```bash
npm install @ttypt/lemma
```

```typescript
import { foldAccents, tokenize, LemmaIndex } from "@ttypt/lemma";

// 1. Unicode Case Folding (lowercasing while preserving language-specific accents)
const folded = foldAccents("Pão de Évora");
console.log(folded); // "pão de évora"

// 2. Tokenization into normalized word units
const tokens = tokenize("O pão fresco na padaria!");
console.log(tokens); // ["pão", "fresco", "na", "padaria"]

// 3. Fast In-Memory Lexical Indexing & BM25-style Search
const index = new LemmaIndex();
index.addDocument(1, "Padaria tradicional com pão fresco");
index.addDocument(2, "Restaurante de peixe grelhado");
index.addDocument(3, "Café com pão de queijo");

const results = index.search("pão");
console.log(results); // [{ id: 1, score: 1 }, { id: 3, score: 1 }]
```

---

## 2. Invariants & Multilingual Features

- **Accent-Sensitive Preserving**: Unlike crude regex lowercasing that destroys diacritics, `lemma_fold` respects Unicode character properties (`Pão` $\rightarrow$ `pão`, `Straße` $\rightarrow$ `straße`, `ÉTÉ` $\rightarrow$ `été`).
- **Instant Search-as-You-Type**: Runs in browser inputs via pre-compiled `dist/lemma.wasm` with sub-millisecond query execution and zero external runtime dependencies.

---

## 3. Dual-Engine Runtime

- **Node.js**: Native C99 addon (`build/Release/lemma_node.node`) linking directly to `liblemma`.
- **Browser & Edge**: Precompiled `dist/lemma.wasm` reactor with zero native compilation prerequisites.

---

## 4. C Development Headers via NPM

```makefile
CFLAGS += -I./node_modules/@ttypt/lemma/include
LDLIBS += -llemma
```

---

## 5. Building from Source

```bash
make npm-build    # Compiles native addon and WASM reactor
make npm-test     # Runs verification test suite
```

## License

BSD 2-Clause License. Copyright (c) 2026, tty-pt.
