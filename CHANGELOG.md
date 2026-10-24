## [1.1.0]

<p align="center">
  <img src="https://raw.githubusercontent.com/tty-pt/liblemma/main/assets/logo.png" alt="liblemma logo" width="240" />
</p>

- **Lossless Subword Tokenizer (`src/tok.c`, `<ttypt/lemma.h>`)**:
  - Added subword tokenization engine (`lemma_tok_encode`, `lemma_tok_decode`, `lemma_tok_stream`) providing lossless tokenization and zero-copy string reconstruction.
  - Bundled static BPE/unigram vocabulary base (`data/vocab.txt`, `src/vocab_base.h`).
- **3-Gram Substring Indexing**:
  - Character 3-gram trigram inverted indexing for fast subword and mid-word search.
- **Redundant `doc_hd` Elimination**:
  - Eliminated the separate document map (`doc_hd`) in favor of direct token array storage and interned terms, cutting memory consumption and cache misses.
- **HTOK Lossless Token Ingestion (`rec_axis_store_typed`)**:
  - Added support for lossless token array ingestion compatible with `libhyle-source` `.tok` containers.
- **Linker Purity & Portable Build**:
  - Removed hardcoded `/home/` and `-Wl,-rpath` linker paths.

## [1.0.0]

<p align="center">
  <img src="https://raw.githubusercontent.com/tty-pt/liblemma/main/assets/logo.png" alt="liblemma logo" width="240" />
</p>

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
