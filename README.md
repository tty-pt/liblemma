<p align="center">
  <img src="assets/logo.png" alt="liblemma logo" width="240" />
</p>

# liblemma

[![C99](https://img.shields.io/badge/C-C99-555?logo=c)](#)
[![BSD-2-Clause](https://img.shields.io/badge/License-BSD--2--Clause-blue)](#)
[![FTS tokenizer](https://img.shields.io/badge/FTS-tokenizer-16A34A)](#)
[![Recall Kernel Lexical Axis](https://img.shields.io/badge/axis-lexical-blueviolet)](#)

> Accent-sensitive search tokenizer, inverted full-text index (`pão ≠ pao`), and recall kernel lexical axis with zero external locale or iconv dependencies.

---

## Why liblemma? (Full-Text Search That Respects Human Languages)

Most search tokenizers collapse or strip accents during ASCII folding:
> *"In Portuguese, 'pão' means bread, while 'pao' means stick/wood. In Spanish, 'año' means year, while 'ano' means anus."*

Standard ASCII transliteration collapses these distinct words, creating false search hits, broken relevance rankings, and embarrassing user results.

`liblemma` solves this in pure C:
- **Diacritics Strictly Preserved**: Lowercases text and normalizes whitespace while strictly preserving accent marks (`pão ≠ pao`).
- **Contiguous Phrase Search**: Finds exact phrases (`"Pão de Açúcar"`) or prefix stems (`cora*` matches `Coração`) in memory-mapped inverted indexes.
- **Zero Locale Dependencies**: Implemented in self-contained C without linking `iconv` or requiring system locale generation (`/etc/locale.gen`).

---

## Search & Recall Integration

`liblemma` provides fast lexical tokenization and inverted index search:
- **Data Engine FTS**: Powers accent-preserving full-text search across string fields in [libhyle](https://github.com/tty-pt/hyle).
- **Recall Kernel (Text Axis)**: Matches stream directly into candidate sets via `rec_axis_fill_tokens` registered with [libcorm](https://github.com/tty-pt/libcorm) (`<ttypt/cormr.h>`).

---

## Diacritic Preservation: Why Accents Matter

Standard ASCII folding collapses accented vowels (`pão` $\rightarrow$ `pao`), causing severe semantic collisions in romance and Germanic languages. `liblemma` lowercases ASCII and Latin-1 characters while strictly preserving diacritic identity:

| Input Text | Standard ASCII Folding | `lemma_fold` Output | Semantic Correctness |
| :--- | :--- | :--- | :--- |
| `Pão` (Bread) | `pao` | `pão` | Distinct from `pau` or `pao` |
| `Coração` (Heart) | `coracao` | `coração` | Accents preserved |
| `München` (Munich) | `munchen` | `münchen` | Umlaut preserved |
| `Café` (Coffee) | `cafe` | `café` | Acute accent preserved |

Zero external dependencies: `liblemma` requires no `iconv` dynamic libraries and no operating system locale files (`/etc/locale.gen`).

---

## Quickstart

Index documents and query accent-sensitive prefixes in under 15 lines:

```c
#include <stdio.h>
#include <stdint.h>
#include <ttypt/lemma.h>

int main(void) {
    // Open in-memory inverted index database
    lemma_db_t *db = lemma_open(0xFF);

    // Index song titles strictly preserving accents
    lemma_index(db, "title", "song_1", "Coração");
    lemma_index(db, "title", "song_2", "Pão de Açúcar");

    // Prefix search for "cora" matches "Coração" (accent preserved)
    int handled = 0;
    lemma_query(db, "title", "cora", 0, &handled);

    printf("Full-text index query executed (handled: %d)\n", handled);
    lemma_close(db);
    return 0;
}
```

Compile and link:
```sh
cc fts_demo.c $(pkg-config --cflags --libs lemma)
```

---

## Core API (The Essential 20%)

| Function | Signature | Description |
| :--- | :--- | :--- |
| `lemma_open` | `lemma_db_t *lemma_open(unsigned mask)` | Open in-memory inverted index table |
| `lemma_index` | `int lemma_index(db, field, row_id, val)` | Tokenize and index a document field |
| `lemma_query` | `uint32_t lemma_query(db, field, q, out_hd, &h)` | Prefix search matching word beginnings |
| `lemma_query_phrase` | `uint32_t lemma_query_phrase(db, f, phrase, out, &h)` | Contiguous multi-word exact phrase query |
| `lemma_fold` | `int lemma_fold(out, outsz, in)` | Lowercase ASCII and Latin-1 while keeping diacritics |
| `rec_axis_fill_tokens` | `int rec_axis_fill_tokens(db, field, q, phrase, set)` | Stream token matches into a sealed recall candidate set |

👉 *For differential un-indexing, phrase query mechanics, and recall ranking contexts, see `include/ttypt/lemma.h`.*

---

## Design Philosophy: Constraints as Superpowers

* **Strict Accent Preservation**: Unlike standard POSIX collations that flatten diacritics (`pao == pão`), `lemma` strictly honors linguistic meaning while lowercasing characters, preventing incorrect lexical collisions.
* **Zero External Dependencies**: Implemented entirely in portable C without linking `iconv` or depending on system locales (`/etc/locale.gen`).
* **Fast Substring Inversion**: Indexes `(field, token) -> row_id` directly in memory using compact [libcorm](https://github.com/tty-pt/libcorm) backing tables.

---

## Installation & Build

### Prebuilt Packages
Prebuilt packages are distributed from tty.pt for Linux (APT / Alpine / Arch / Fedora-RHEL), macOS (Homebrew), Windows (winget / MSYS2), and OpenBSD. Follow the [installation instructions](https://github.com/tty-pt/ci/blob/main/docs/install.md) and use **liblemma** as the package name.

### Build from Source
```sh
make                  # builds lib/liblemma.so
make test             # run tokenization, property, and query tests
sudo make install     # install headers and library to system
```

## License

BSD 2-Clause License. Copyright (c) 2026, tty-pt. See `LICENSE`.
