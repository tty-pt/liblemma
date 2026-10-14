<p align="center">
  <img src="assets/logo.png" alt="libstoma logo" width="240" />
</p>

# libstoma

[![C99](https://img.shields.io/badge/C-C99-555?logo=c)](#)
[![BSD-2-Clause](https://img.shields.io/badge/License-BSD--2--Clause-blue)](#)
[![FTS tokenizer](https://img.shields.io/badge/FTS-tokenizer-16A34A)](#)
[![Recall Kernel Lexical Axis](https://img.shields.io/badge/axis-lexical-blueviolet)](#)

> Accent-sensitive search tokenizer, inverted full-text index (`pão ≠ pao`), and recall kernel lexical axis with zero external locale or iconv dependencies.

---

## Search & Recall Integration

`libstoma` provides fast lexical tokenization and inverted index search:
- **Data Engine FTS**: Powers accent-preserving full-text search across string fields in [libhyle](https://github.com/tty-pt/hyle).
- **Recall Kernel (Text Axis)**: Matches stream directly into candidate sets via `rec_axis_fill_tokens` in [libcorm](https://github.com/tty-pt/libcorm).

---

## Quickstart

Index documents and query accent-sensitive prefixes in under 15 lines:

```c
#include <stdio.h>
#include <stdint.h>
#include <stoma/stoma.h>

int main(void) {
    // Open in-memory inverted index database
    stoma_db_t *db = stoma_open(0xFF);

    // Index song titles strictly preserving accents
    stoma_index(db, "title", "song_1", "Coração");
    stoma_index(db, "title", "song_2", "Pão de Açúcar");

    // Prefix search for "cora" matches "Coração" (accent preserved)
    int handled = 0;
    stoma_query(db, "title", "cora", 0, &handled);

    printf("Full-text index query executed (handled: %d)\n", handled);
    stoma_close(db);
    return 0;
}
```

Compile and link:
```sh
cc fts_demo.c $(pkg-config --cflags --libs stoma)
```

---

## Core API (The Essential 20%)

| Function | Signature | Description |
| :--- | :--- | :--- |
| `stoma_open` | `stoma_db_t *stoma_open(unsigned mask)` | Open in-memory inverted index table |
| `stoma_index` | `int stoma_index(db, field, row_id, val)` | Tokenize and index a document field |
| `stoma_query` | `uint32_t stoma_query(db, field, q, out_hd, &h)` | Prefix search matching word beginnings |
| `stoma_query_phrase` | `uint32_t stoma_query_phrase(...)` | Contiguous exact phrase query |
| `stoma_fold` | `int stoma_fold(out, outsz, in)` | Lowercase ASCII and Latin-1 while keeping diacritics |
| `rec_axis_fill_tokens` | `int rec_axis_fill_tokens(db, field, q, phrase, set)` | Stream token matches into a sealed recall candidate set |

👉 *For differential un-indexing, phrase query mechanics, and recall ranking contexts, see `include/stoma/stoma.h`.*

---

## Design Philosophy: Diacritics Matter

* **Strict Accent Preservation**: Unlike standard POSIX collations that flatten diacritics (`pao == pão`), `stoma` strictly honors linguistic meaning while lowercasing characters, preventing incorrect lexical collisions.
* **Zero External Dependencies**: Implemented entirely in portable C without linking `iconv` or depending on system locales (`/etc/locale.gen`).
* **Fast Substring Inversion**: Indexes `(field, token) -> row_id` directly in memory using compact [libcorm](https://github.com/tty-pt/libcorm) backing tables.

---

## Installation & Build

### Prebuilt Packages
Prebuilt packages are distributed from tty.pt for Linux (APT / Alpine / Arch / Fedora-RHEL), macOS (Homebrew), Windows (winget / MSYS2), and OpenBSD. Follow the [installation instructions](https://github.com/tty-pt/ci/blob/main/docs/install.md) and use **libstoma** as the package name.

### Build from Source
```sh
make                  # builds lib/libstoma.so
make test             # run tokenization, property, and query tests
sudo make install     # install headers and library to system
```

## License

BSD 2-Clause License. Copyright (c) 2026, tty-pt. See `LICENSE`.
