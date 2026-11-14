## [1.4.0] - 2026-11-14

- **Complete Node-API Bindings & HTOK v4 UTF-8 Text Indexing (`src/liblemma.c`, `npm/native/lemma_napi.c`, `npm/dist/index.js`)**:
  - Added HTOK v4 `type_tag == 2` (`CORM_HTOK_TYPE_UTF8`) text field indexing in `lemma_store_typed` alongside varint token arrays, and preserved `CORM_AXIS_PRIMARY` pointer lifetime across primary-seeded rebuilds.
  - Bound `tokenize`, `open`/`close`/`clear`, `index`/`indexRef`/`unindex`/`unindexRef`, `storeTyped`, `fillTokens`, `rank`, `search`, and `tokEncode`/`tokDecode` in `lemma_napi.c`.
  - Removed JS `Map` fallback in `LemmaIndex` and exported `encodeTokens`/`decodeTokens`.
  - Requires `@tty-pt/corm` >=2.4.0 (`^2.4.0`) and `@tty-pt/stele` >=1.7.0 (`^1.7.0`).

## [1.3.2] - 2026-11-13

- **Packaging**:
  - Requires `@tty-pt/corm` >=2.3.3 (`^2.3.3`) and `@tty-pt/stele` >=1.6.2 (`^1.6.2`).
  - The npm tarball is built once by CI, with a self-contained native addon per platform (`prebuilds/<os>-<cpu>/`) next to `lemma.wasm`, and published as verified.

## [1.3.1] - 2026-11-13

- **Freestanding WASM Reactor & Disjoint Global-Base (`lemma.wasm`)**:
  - Eliminated `stdio`/WASI imports in `src/token.c` and rebuilt `lemma.wasm` as a pure shared-memory reactor at `--global-base=1310720`.
  - Upgraded `@tty-pt/corm` to `>=2.3.2` (`^2.3.2`) and `@tty-pt/stele` to `>=1.6.1` (`^1.6.1`).

## [1.3.0] - 2026-11-13

- **Substrate & Corm v2.3.1 Alignment**:
  - Upgraded dependencies to @tty-pt/corm@^2.3.1 (>=2.3.1) and @tty-pt/stele@^1.6.0 (>=1.6.0).
  - Maintained lossless tokenization and subword search on shared linear memory.

## [1.2.2] - 2026-11-03

- **Lossless Subword Tokenizer & HTOK Token Ingestion (`src/tok.c`, `<ttypt/lemma.h>`)**:
  - Added subword tokenization engine (`lemma_tok_encode`, `lemma_tok_decode`, `lemma_tok_stream`) providing lossless tokenization and zero-copy string reconstruction.
  - Bundled static BPE/unigram vocabulary base (`data/vocab.txt`, `src/vocab_base.h`).
  - Added 3-gram character trigram inverted indexing for fast subword and mid-word search.
  - Added support for lossless token array ingestion compatible with `libhyle-source` `.tok` containers (`rec_axis_store_typed`).
- **Standalone `@tty-pt/lemma` Package & WebAssembly Engine**:
  - Published `@tty-pt/lemma` providing Node-API native bindings and pure WebAssembly fallback (`lemma.wasm`).
  - Added `npm/wasm/lemma_wasm.c` exposing `lemma_wasm_tokenize` and `lemma_fold` using `ISO_EXPORT`.
  - Adopted strict `peerDependencies` (`@tty-pt/stele: ">=1.5.0"`, `@tty-pt/corm: ">=2.2.0"`) and `devDependencies` (`@tty-pt/stele: "^1.5.2"`, `@tty-pt/corm: "^2.2.4"`).
  - Migrated build to centralized `make wasm` in `external/mk/include.mk` with post-link `wasm-opt -O3` optimization (dropping binary size from 135 KB to 119 KB).

## [1.0.0]

- **First stable release** — search tokenization and accent-sensitive inverted index for hyle's full-text search, backed by `libcorm`.
- **Zero-Cost FTS & Term Interning**: interned term dictionary (`term_hd` mapping `field\ttoken` $\to$ `uint32_t term_id` with reverse map `term_rev_hd`) eliminating redundant token string duplication across documents and sessions.
- **Compact Fixed-Width Postings**: replaced variable-length token strings in inverted index keys with `%08x\t<row_id>` in `db->hd`, reducing heap footprint and fragmentation while preserving prefix search order.
- **Accent-preserving folding**: `lemma_fold` lowercases ASCII (A-Z) and Latin-1 supplement uppercase while strictly preserving diacritics — `pão` ≠ `pao`.
- **Inverted index**: `lemma_open`/`lemma_close`/`lemma_clear`, `lemma_index`/`lemma_unindex` (differential inverse proportional to the row's own tokens, never O(store), idempotent) over `(field, token) → row_id`.
- **`rec_ref_t`-native conveniences**: `lemma_index_ref`/`lemma_unindex_ref` for callers whose id is already a `rec_ref_t`.
- **Prefix and phrase queries**: `lemma_query` (word-beginning match — `cor` matches `coração` via range scan on `term_hd`) and `lemma_query_phrase` (contiguous multi-word sequences in exact token order across line breaks and punctuation), both with `uint32_t` row handles.
- **Recall-kernel form**: `rec_axis_fill_tokens` (exact lexical set, `phrase` flag, additive fill + seal) and the `lemma_rank` FTS scorer (score = matched / token_count of the folded field text, shorter docs rank higher on ties) — composes as tokens ∩ geo ∩ time → top-k via `rec_query_run`.
- **Recall-kernel storage adapters**: `rec_axis_store` / `rec_axis_unstore` / `rec_axis_readback` (replace-in-place, one ref owns one doc; folded text as a NUL-joined blob).
- **Token utilities**: `lemma_tokenize` word iterator, `lemma_list_normalize`/`lemma_list_contains`/`lemma_list_append` newline-separated helpers.
- **Pure C, zero encoding dependencies**: no `iconv` or system locale — predictable execution.
