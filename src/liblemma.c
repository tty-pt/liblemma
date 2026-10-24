#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <dirent.h>
#include <libgen.h>
#include <sys/stat.h>
#include <time.h>
#include <ctype.h>
#include <ttypt/corm.h>
#include "ttypt/lemma.h"

static inline int is_word_char(unsigned char c)
{
	return isalnum(c) || c >= 0x80;
}

#define LEMMA_MAX_TOKENS 64

/* Declared in token.c (internal). */
void lemma_tokenize(
        const char *folded, void (*cb)(const char *tok, size_t len, void *user),
        void *user);

struct lemma_db {
	unsigned hd;          /* CM_SORTED, CM_STR → CM_STR inverted index: "term_id\trow_id" → "" */
	unsigned doc_tok_hd;  /* CM_SORTED, CM_STR → doc_tok_type: token stream per (field,row) */
	unsigned term_hd;     /* CM_SORTED, CM_STR → CM_U32: "field\ttoken" → term_id */
	unsigned term_rev_hd; /* CM_U32 → CM_STR: term_id → "field\ttoken" */
	uint32_t next_term_id;/* Monotonic term ID generator starting at 1 */
};

static size_t doc_tok_measure(const void *data)
{
	if (!data)
		return 0;
	const uint16_t *count = (const uint16_t *)data;
	return sizeof(uint16_t) + (size_t)(*count) * sizeof(uint16_t);
}

static uint32_t g_doc_tok_type = 0;

/* D14 axis-contributed CLI option state (rec_axis_config_arg, round 2).
 * decode merges it into the bare/empty spec path (searchable when --query
 * is present; field defaults to "text") and fills fields a non-empty leaf
 * omits. */
static char *lemma_cli_query;
static char *lemma_cli_field;
static int lemma_cli_phrase;
static size_t lemma_cli_matched;
static int lemma_cli_field_set, lemma_cli_phrase_set, lemma_cli_matched_set;

lemma_db_t *lemma_open(unsigned mask)
{
	lemma_db_t *db;

	if (!g_doc_tok_type) {
		g_doc_tok_type = corm_mreg(doc_tok_measure);
	}

	db = calloc(1, sizeof(*db));
	if (!db)
		return NULL;
	db->hd = corm_open(NULL, NULL, CM_STR, CM_STR, mask, CM_SORTED);
	if (!db->hd) {
		free(db);
		return NULL;
	}
	db->doc_tok_hd = corm_open(NULL, NULL, CM_STR, g_doc_tok_type, mask, CM_SORTED);
	if (!db->doc_tok_hd) {
		corm_close(db->hd);
		free(db);
		return NULL;
	}
	db->term_hd = corm_open(NULL, NULL, CM_STR, CM_U32, mask, CM_SORTED);
	if (!db->term_hd) {
		corm_close(db->hd);
		corm_close(db->doc_tok_hd);
		free(db);
		return NULL;
	}
	db->term_rev_hd = corm_open(NULL, NULL, CM_U32, CM_STR, mask, 0);
	if (!db->term_rev_hd) {
		corm_close(db->hd);
		corm_close(db->doc_tok_hd);
		corm_close(db->term_hd);
		free(db);
		return NULL;
	}
	db->next_term_id = 1;
	return db;
}

void lemma_close(lemma_db_t *db)
{
	if (!db)
		return;
	corm_close(db->hd);
	corm_close(db->doc_tok_hd);
	corm_close(db->term_hd);
	corm_close(db->term_rev_hd);
	free(db);
}

void lemma_clear(lemma_db_t *db)
{
	if (!db)
		return;
	corm_drop(db->hd);
	corm_drop(db->doc_tok_hd);
	corm_drop(db->term_hd);
	corm_drop(db->term_rev_hd);
	db->next_term_id = 1;
}

/* ---- term interning ---- */

static uint32_t lemma_term_intern(lemma_db_t *db, const char *field, const char *tok, size_t len)
{
	size_t fld = strlen(field);
	char *key = malloc(fld + 1 + len + 1);
	if (!key)
		return 0;
	memcpy(key, field, fld);
	key[fld] = '\t';
	memcpy(key + fld + 1, tok, len);
	key[fld + 1 + len] = '\0';

	const uint32_t *existing = (const uint32_t *)corm_get(db->term_hd, key);
	if (existing) {
		uint32_t tid = *existing;
		free(key);
		return tid;
	}

	uint32_t tid = db->next_term_id++;
	corm_put(db->term_hd, key, &tid);
	corm_put(db->term_rev_hd, &tid, key);
	free(key);
	return tid;
}

static uint32_t lemma_term_lookup(lemma_db_t *db, const char *field, const char *tok, size_t len)
{
	size_t fld = strlen(field);
	char *key = malloc(fld + 1 + len + 1);
	if (!key)
		return 0;
	memcpy(key, field, fld);
	key[fld] = '\t';
	memcpy(key + fld + 1, tok, len);
	key[fld + 1 + len] = '\0';

	const uint32_t *existing = (const uint32_t *)corm_get(db->term_hd, key);
	uint32_t tid = existing ? *existing : 0;
	free(key);
	return tid;
}

/* ---- index ---- */

typedef struct {
	lemma_db_t *db;
	const char *field;
	const char *row_id;
} index_ctx_t;

static void index_token(const char *tok, size_t len, void *user)
{
	index_ctx_t *ctx = (index_ctx_t *)user;
	uint32_t tid = lemma_term_intern(ctx->db, ctx->field, tok, len);
	if (!tid)
		return;

	size_t rid = strlen(ctx->row_id);
	char *key = malloc(8 + 1 + rid + 1);
	if (!key)
		return;
	snprintf(key, 8 + 1 + rid + 1, "%08x\t%s", tid, ctx->row_id);
	corm_put(ctx->db->hd, key, "");
	free(key);
}

static void store_doc_tokens(lemma_db_t *db, const char *field, const char *row_id,
                             const uint16_t *tokens, size_t n_tokens)
{
	size_t fld = strlen(field);
	size_t rid = strlen(row_id);
	size_t dlen = fld + 1 + rid;
	char *dkey = malloc(dlen + 1);
	if (!dkey)
		return;
	memcpy(dkey, field, fld);
	dkey[fld] = '\t';
	memcpy(dkey + fld + 1, row_id, rid);
	dkey[dlen] = '\0';

	size_t payload_bytes = sizeof(uint16_t) + n_tokens * sizeof(uint16_t);
	uint16_t *payload = malloc(payload_bytes);
	if (payload) {
		payload[0] = (uint16_t)n_tokens;
		memcpy(payload + 1, tokens, n_tokens * sizeof(uint16_t));
		corm_put(db->doc_tok_hd, dkey, payload);
		free(payload);
	}
	free(dkey);
}

int lemma_index(
        lemma_db_t *db, const char *field, const char *row_id,
        const char *value)
{
	index_ctx_t ctx;
	size_t vlen;
	char *folded;

	if (!db || !field || !row_id || !value)
		return -1;
	/* The fold never grows its output, so strlen+1 always fits. */
	vlen = strlen(value);
	folded = malloc(vlen + 1);
	if (!folded)
		return -1;
	if (lemma_fold(folded, vlen + 1, value) < 0) {
		free(folded);
		return 0;
	}
	ctx.db = db;
	ctx.field = field;
	ctx.row_id = row_id;
	lemma_tokenize(folded, index_token, &ctx);

	/* Store tokenized stream in doc_tok_hd (doc_hd eliminated) */
	uint16_t toks_buf[2048];
	size_t n_toks = 0;
	if (lemma_tok_encode(NULL, value, strlen(value), toks_buf, 2048, &n_toks) == 0) {
		store_doc_tokens(db, field, row_id, toks_buf, n_toks);
	}

	free(folded);
	return 0;
}

int lemma_index_tokens(lemma_db_t *db, const char *field, const char *row_id,
                       const uint16_t *tokens, size_t n_tokens)
{
	if (!db || !field || !row_id || !tokens)
		return -1;

	const lemma_tok_vocab_t *vocab = lemma_tok_vocab_default();
	if (!vocab)
		return -1;

	/* Selective Lexical Indexing: index all words, skip delimiters */
	size_t i = 0;
	while (i < n_tokens) {
		uint16_t tid = tokens[i];
		char word_buf[128];
		size_t wlen = 0;

		if (vocab && lemma_tok_is_word(vocab, tid)) {
			char dec[128];
			size_t bw = 0;
			if (lemma_tok_decode(vocab, &tid, 1, dec, sizeof(dec), &bw) == 0 && bw > 0) {
				if (lemma_fold(word_buf, sizeof(word_buf), dec) >= 0)
					wlen = strlen(word_buf);
			}
			i++;
		} else if (tid < 256 && is_word_char((unsigned char)tid)) {
			char byte_buf[128];
			size_t blen = 0;
			while (i < n_tokens && tokens[i] < 256 && is_word_char((unsigned char)tokens[i]) && blen < sizeof(byte_buf) - 1) {
				byte_buf[blen++] = (char)tokens[i++];
			}
			byte_buf[blen] = '\0';
			if (lemma_fold(word_buf, sizeof(word_buf), byte_buf) >= 0)
				wlen = strlen(word_buf);
		} else {
			i++;
			continue;
		}

		if (wlen > 0) {
			uint32_t term_id = lemma_term_intern(db, field, word_buf, wlen);
			if (term_id) {
				size_t rid = strlen(row_id);
				char *key = malloc(8 + 1 + rid + 1);
				if (key) {
					snprintf(key, 8 + 1 + rid + 1, "%08x\t%s", term_id, row_id);
					corm_put(db->hd, key, "");
					free(key);
				}
			}
		}
	}

	store_doc_tokens(db, field, row_id, tokens, n_tokens);
	return 0;
}

int lemma_index_ref(
        lemma_db_t *db, const char *field, rec_ref_t row_id,
        const char *value)
{
	char rid[24];

	snprintf(rid, sizeof(rid), "%llu", (unsigned long long)row_id);
	return lemma_index(db, field, rid, value);
}

/* ---- unindex (the differential inverse of index) ---- */

int lemma_unindex(lemma_db_t *db, const char *field, const char *row_id)
{
	size_t fld;
	size_t rid;
	size_t dlen;
	char *dkey;
	const void *raw;

	if (!db || !field || !row_id)
		return -1;

	fld = strlen(field);
	rid = strlen(row_id);
	dlen = fld + 1 + rid;
	dkey = malloc(dlen + 1);
	if (!dkey)
		return -1;
	memcpy(dkey, field, fld);
	dkey[fld] = '\t';
	memcpy(dkey + fld + 1, row_id, rid);
	dkey[dlen] = '\0';
	raw = corm_get(db->doc_tok_hd, dkey);
	if (!raw) {
		free(dkey);
		return 0; /* absent (field,row): idempotent no-op */
	}

	const uint16_t *hdr = (const uint16_t *)raw;
	uint16_t n_tokens = hdr[0];
	const uint16_t *tokens = hdr + 1;
	const lemma_tok_vocab_t *vocab = lemma_tok_vocab_default();

	size_t t = 0;
	while (t < n_tokens) {
		uint16_t tid = tokens[t];
		char word_buf[128];
		size_t wlen = 0;

		if (vocab && lemma_tok_is_word(vocab, tid)) {
			char dec[128];
			size_t bw = 0;
			if (lemma_tok_decode(vocab, &tid, 1, dec, sizeof(dec), &bw) == 0 && bw > 0) {
				if (lemma_fold(word_buf, sizeof(word_buf), dec) >= 0)
					wlen = strlen(word_buf);
			}
			t++;
		} else if (tid < 256 && is_word_char((unsigned char)tid)) {
			char byte_buf[128];
			size_t blen = 0;
			while (t < n_tokens && tokens[t] < 256 && is_word_char((unsigned char)tokens[t]) && blen < sizeof(byte_buf) - 1) {
				byte_buf[blen++] = (char)tokens[t++];
			}
			byte_buf[blen] = '\0';
			if (lemma_fold(word_buf, sizeof(word_buf), byte_buf) >= 0)
				wlen = strlen(word_buf);
		} else {
			t++;
			continue;
		}

		if (wlen > 0) {
			uint32_t term_id = lemma_term_lookup(db, field, word_buf, wlen);
			if (term_id) {
				size_t rid_len = strlen(row_id);
				char *key = malloc(8 + 1 + rid_len + 1);
				if (key) {
					snprintf(key, 8 + 1 + rid_len + 1, "%08x\t%s", term_id, row_id);
					corm_del(db->hd, key);
					free(key);
				}
			}
		}
	}

	corm_del(db->doc_tok_hd, dkey);
	free(dkey);
	return 0;
}

int lemma_unindex_ref(lemma_db_t *db, const char *field, rec_ref_t row_id)
{
	char rid[24];

	snprintf(rid, sizeof(rid), "%llu", (unsigned long long)row_id);
	return lemma_unindex(db, field, rid);
}

/* ---- rec_axis store / unstore / readback (Phase 2A) ---- */

static int lemma_store_text(lemma_db_t *db, rec_ref_t ref, const char *value)
{
	if (!db || !value) {
		errno = EINVAL;
		return -1;
	}
	if (!*value) { /* an empty string indexes nothing: loud reject */
		errno = EINVAL;
		return -1;
	}
	/* Replace-in-place: the ref owns exactly one doc, so erase any
	 * previous entry first (idempotent when absent). */
	if (lemma_unindex_ref(db, LEMMA_AXIS_TEXT_FIELD, ref) != 0)
		return -1;
	return lemma_index_ref(db, LEMMA_AXIS_TEXT_FIELD, ref, value);
}

int rec_axis_store(void *ctx, const char *spec, rec_ref_t ref,
	const char *value)
{
	(void)spec;
	return lemma_store_text((lemma_db_t *)ctx, ref, value);
}

#define HTOK_MAGIC 0x48544F4B

static inline uint32_t lemma_read_varint(const uint8_t **p_ptr, const uint8_t *end)
{
	uint32_t val = 0;
	int shift = 0;
	while (*p_ptr < end) {
		uint8_t b = *(*p_ptr)++;
		val |= (uint32_t)(b & 0x7f) << shift;
		if (!(b & 0x80))
			break;
		shift += 7;
	}
	return val;
}

static int lemma_store_typed(void *ctx, const char *spec, rec_ref_t ref,
                               const void *blob, size_t len, uint32_t qtype)
{
	lemma_db_t *db = ctx;
	char rid[32];

	(void)spec;
	if (!db || !blob || len == 0) {
		errno = EINVAL;
		return -1;
	}
	if (qtype == CM_STR) {
		return rec_axis_store(ctx, spec, ref, (const char *)blob);
	}

	snprintf(rid, sizeof(rid), "%u", (unsigned)ref);

	/* Check for HTOK container format */
	if (len >= 8 && *(const uint32_t *)blob == HTOK_MAGIC) {
		uint16_t version = *(const uint16_t *)((const uint8_t *)blob + 4);
		const uint8_t *p = (const uint8_t *)blob + 6;
		const uint8_t *end = (const uint8_t *)blob + len;
		uint16_t field_count = 0;

		/* Clear previous index entries for this ref under text field */
		lemma_unindex_ref(db, LEMMA_AXIS_TEXT_FIELD, ref);

		if (version >= 4) {
			if (len < 12) return -1;
			uint8_t flags = *((const uint8_t *)blob + 11);
			p = (const uint8_t *)blob + 12;

			if (flags & 0x01) { /* supersedes */
				lemma_read_varint(&p, end);
			}
			if (flags & 0x02) { /* supersededBy */
				lemma_read_varint(&p, end);
			}
			if (flags & 0x04) { /* evidence delta array */
				uint32_t evid_count = lemma_read_varint(&p, end);
				for (uint32_t i = 0; i < evid_count && p < end; i++) {
					lemma_read_varint(&p, end);
				}
			}

			uint32_t fc = lemma_read_varint(&p, end);
			for (uint32_t i = 0; i < fc && p < end; i++) {
				if (p >= end) break;
				uint8_t var_tag = *p++;
				uint8_t field_id = (var_tag >> 3) & 0x1f;
				uint8_t type_tag = var_tag & 0x07;
				if (field_id == 31) {
					if (p >= end) break;
					uint8_t nlen = *p++;
					if (p + nlen > end) break;
					p += nlen;
				}
				uint32_t data_len = lemma_read_varint(&p, end);
				if (p + data_len > end) break;

				if (field_id == 0 && type_tag == 1) {
					size_t max_tok = data_len + 16;
					uint16_t *tokens = malloc(max_tok * sizeof(uint16_t));
					if (tokens) {
						size_t ntok = lemma_tok_unpack_varint(p, data_len, tokens, max_tok);
						size_t cap = ntok * 16 + 256;
						char *dec = malloc(cap);
						size_t bw = 0;
						if (dec && lemma_tok_decode(NULL, tokens, ntok, dec, cap, &bw) == 0 && bw > 0) {
							lemma_index(db, LEMMA_AXIS_TEXT_FIELD, rid, dec);
							store_doc_tokens(db, LEMMA_AXIS_TEXT_FIELD, rid, tokens, ntok);
						} else {
							lemma_index_tokens(db, LEMMA_AXIS_TEXT_FIELD, rid, tokens, ntok);
						}
						free(dec);
						free(tokens);
					}
				}
				p += data_len;
			}
			return 0;
		} else if (version >= 3) {
			if (len < 16) return -1;
			uint8_t flags = *((const uint8_t *)blob + 13);
			size_t hdr_size = 14;
			if (flags & 0x01) hdr_size += 4; /* supersedes */
			if (flags & 0x02) hdr_size += 4; /* supersededBy */
			if (flags & 0x04) {
				if (hdr_size + 2 > len) return -1;
				uint16_t evid_count = *(const uint16_t *)((const uint8_t *)blob + hdr_size);
				hdr_size += 2 + (size_t)evid_count * 4;
			}
			if (hdr_size + 2 > len) return -1;
			memcpy(&field_count, (const uint8_t *)blob + hdr_size, 2);
			p = (const uint8_t *)blob + hdr_size + 2;

			for (uint16_t i = 0; i < field_count && p < end; i++) {
				if (p + 1 > end) break;
				uint8_t nlen = *p++;
				if (p + nlen + 3 > end) break;
				char fname[64];
				size_t copylen = nlen < sizeof(fname) - 1 ? nlen : sizeof(fname) - 1;
				memcpy(fname, p, copylen);
				fname[copylen] = '\0';
				p += nlen;

				uint8_t type_tag = *p++;
				uint16_t data_len = 0;
				memcpy(&data_len, p, 2); p += 2;
				if (p + data_len > end) break;

				if (type_tag == 1 && (strcmp(fname, "text") == 0 || strcmp(fname, LEMMA_AXIS_TEXT_FIELD) == 0)) {
					size_t max_tok = data_len + 16;
					uint16_t *tokens = malloc(max_tok * sizeof(uint16_t));
					if (tokens) {
						size_t ntok = lemma_tok_unpack_varint(p, data_len, tokens, max_tok);
						size_t cap = ntok * 16 + 256;
						char *dec = malloc(cap);
						size_t bw = 0;
						if (dec && lemma_tok_decode(NULL, tokens, ntok, dec, cap, &bw) == 0 && bw > 0) {
							lemma_index(db, LEMMA_AXIS_TEXT_FIELD, rid, dec);
							store_doc_tokens(db, LEMMA_AXIS_TEXT_FIELD, rid, tokens, ntok);
						} else {
							lemma_index_tokens(db, LEMMA_AXIS_TEXT_FIELD, rid, tokens, ntok);
						}
						free(dec);
						free(tokens);
					}
				}
				p += data_len;
			}
			return 0;
		} else if (version >= 2) {
			if (len < 26) return -1;
			uint16_t evid_count = *(const uint16_t *)((const uint8_t *)blob + 22);
			size_t hdr_size = 24 + (size_t)evid_count * 4;
			if (hdr_size + 2 > len) return -1;
			p = (const uint8_t *)blob + hdr_size;
			memcpy(&field_count, p, 2); p += 2;
		} else {
			memcpy(&field_count, p, 2); p += 2;
		}

		for (uint16_t i = 0; i < field_count && p < end; i++) {
			if (p + 1 > end) break;
			uint8_t nlen = *p++;
			if (p + nlen + 2 > end) break;
			char fname[64];
			size_t copylen = nlen < sizeof(fname) - 1 ? nlen : sizeof(fname) - 1;
			memcpy(fname, p, copylen);
			fname[copylen] = '\0';
			p += nlen;

			uint16_t ntok = 0;
			memcpy(&ntok, p, 2); p += 2;
			if (p + (size_t)ntok * 2 > end) break;

			const uint16_t *tokens = (const uint16_t *)p;
			p += (size_t)ntok * 2;

			/* Index text field */
			if (strcmp(fname, "text") == 0 || strcmp(fname, LEMMA_AXIS_TEXT_FIELD) == 0) {
				size_t cap = (size_t)ntok * 16 + 256;
				char *dec = malloc(cap);
				size_t bw = 0;
				if (dec && lemma_tok_decode(NULL, tokens, ntok, dec, cap, &bw) == 0 && bw > 0) {
					lemma_index(db, LEMMA_AXIS_TEXT_FIELD, rid, dec);
					store_doc_tokens(db, LEMMA_AXIS_TEXT_FIELD, rid, tokens, ntok);
				} else {
					lemma_index_tokens(db, LEMMA_AXIS_TEXT_FIELD, rid, tokens, ntok);
				}
				free(dec);
			}
		}
		return 0;
	}

	/* Raw token stream: uint16_t count, uint16_t tokens[] */
	if (len >= 2) {
		const uint16_t *u16 = (const uint16_t *)blob;
		size_t n_tokens = u16[0];
		if (sizeof(uint16_t) + n_tokens * sizeof(uint16_t) <= len) {
			lemma_unindex_ref(db, LEMMA_AXIS_TEXT_FIELD, ref);
			size_t cap = n_tokens * 16 + 256;
			char *dec = malloc(cap);
			size_t bw = 0;
			if (dec && lemma_tok_decode(NULL, u16 + 1, n_tokens, dec, cap, &bw) == 0 && bw > 0) {
				lemma_index(db, LEMMA_AXIS_TEXT_FIELD, rid, dec);
				store_doc_tokens(db, LEMMA_AXIS_TEXT_FIELD, rid, u16 + 1, n_tokens);
				free(dec);
				return 0;
			}
			free(dec);
			return lemma_index_tokens(db, LEMMA_AXIS_TEXT_FIELD, rid, u16 + 1, n_tokens);
		}
	}

	/* Fallback: text payload stored in blob handle */
	char *tmp = malloc(len + 1);
	if (tmp) {
		memcpy(tmp, blob, len);
		tmp[len] = '\0';
		int rc = lemma_store_text(db, ref, tmp);
		free(tmp);
		return rc;
	}

	return -1;
}

int rec_axis_store_typed(void *ctx, const char *spec, rec_ref_t ref,
                         const void *blob, size_t len, uint32_t qtype)
{
	return lemma_store_typed(ctx, spec, ref, blob, len, qtype);
}

int rec_axis_unstore(void *ctx, rec_ref_t ref)
{
	lemma_db_t *db = ctx;

	if (!db) {
		errno = EINVAL;
		return -1;
	}
	/* lemma_unindex is already absent → 0; the idempotent contract. */
	return lemma_unindex_ref(db, LEMMA_AXIS_TEXT_FIELD, ref);
}

int rec_axis_readback(void *ctx, rec_ref_t ref, char **blob_out,
	size_t *n_out)
{
	lemma_db_t *db = ctx;
	char rid[24];
	size_t fld;
	size_t rlen;
	char *dkey;
	const void *raw;

	if (blob_out)
		*blob_out = NULL;
	if (n_out)
		*n_out = 0;
	if (!db || !blob_out || !n_out) {
		errno = EINVAL;
		return -1;
	}

	snprintf(rid, sizeof(rid), "%llu", (unsigned long long)ref);
	fld = strlen(LEMMA_AXIS_TEXT_FIELD);
	rlen = strlen(rid);
	dkey = malloc(fld + 1 + rlen + 1);
	if (!dkey)
		return -1;
	memcpy(dkey, LEMMA_AXIS_TEXT_FIELD, fld);
	dkey[fld] = '\t';
	memcpy(dkey + fld + 1, rid, rlen);
	dkey[fld + 1 + rlen] = '\0';
	raw = corm_get(db->doc_tok_hd, dkey);
	free(dkey);
	if (!raw)
		return 0; /* absent → NULL/0, still 0 */

	const uint16_t *hdr = (const uint16_t *)raw;
	uint16_t n_tokens = hdr[0];
	const uint16_t *tokens = hdr + 1;

	size_t cap = (size_t)n_tokens * 16 + 256;
	char *buf = malloc(cap);
	if (!buf)
		return -1;

	size_t bw = 0;
	if (lemma_tok_decode(NULL, tokens, n_tokens, buf, cap, &bw) != 0) {
		free(buf);
		return -1;
	}

	char *folded = malloc(bw + 1);
	if (!folded) {
		free(buf);
		return -1;
	}
	lemma_fold(folded, bw + 1, buf);
	free(buf);

	*blob_out = folded;
	*n_out = strlen(folded);
	return 0;
}

/* ---- query ---- */

typedef struct {
	const char *toks[LEMMA_MAX_TOKENS];
	size_t len[LEMMA_MAX_TOKENS];
	size_t n;
} collect_ctx_t;

static void collect_token(const char *tok, size_t len, void *user)
{
	collect_ctx_t *c = (collect_ctx_t *)user;

	if (c->n < LEMMA_MAX_TOKENS) {
		c->toks[c->n] = tok;
		c->len[c->n] = len;
		c->n++;
	}
}

/* ---- phrase verification ---- */

/* ---- recall-kernel adapter ---- */

typedef struct {
	unsigned hd;    /* raw-path output handle            */
	rec_set_t *set; /* kernel candidate set              */
	int err;        /* strict decimal-parse failure flag */
} lemma_dest_t;

static void lemma_dest_emit(lemma_dest_t *d, const char *row_id)
{
	if (d->hd) {
		corm_put(d->hd, row_id, "");
		return;
	}
	if (d->set) {
		char *end;
		unsigned long long v;

		errno = 0;
		if (row_id[0] < '0' || row_id[0] > '9') {
			d->err = 1;
			return;
		}
		v = strtoull(row_id, &end, 10);
		if (!end || *end != '\0' || errno == ERANGE) {
			d->err = 1;
			return;
		}
		if (v > UINT32_MAX) { /* rec_ref_t is u32: never alias upward */
			d->err = 1;
			return;
		}
		rec_set_push(d->set, (rec_ref_t)v);
	}
}

static int doc_tok_matches_phrase(const uint16_t *tokens, size_t n_tokens, const collect_ctx_t *q)
{
	if (!tokens || n_tokens == 0 || !q || q->n == 0)
		return 0;

	const lemma_tok_vocab_t *vocab = lemma_tok_vocab_default();
	if (!vocab)
		return 0;

	char word_bufs[256][64];
	size_t word_lens[256];
	size_t n_words = 0;

	size_t t = 0;
	while (t < n_tokens && n_words < 256) {
		uint16_t tid = tokens[t];

		if (lemma_tok_is_word(vocab, tid)) {
			char dec[128];
			size_t bw = 0;
			if (lemma_tok_decode(vocab, &tid, 1, dec, sizeof(dec), &bw) == 0 && bw > 0) {
				char folded[128];
				if (lemma_fold(folded, sizeof(folded), dec) >= 0) {
					size_t flen = strlen(folded);
					if (flen >= sizeof(word_bufs[0]))
						flen = sizeof(word_bufs[0]) - 1;
					memcpy(word_bufs[n_words], folded, flen);
					word_bufs[n_words][flen] = '\0';
					word_lens[n_words] = flen;
					n_words++;
				}
			}
			t++;
		} else if (tid < 256 && is_word_char((unsigned char)tid)) {
			char byte_buf[128];
			size_t blen = 0;
			while (t < n_tokens && tokens[t] < 256 && is_word_char((unsigned char)tokens[t]) && blen < sizeof(byte_buf) - 1) {
				byte_buf[blen++] = (char)tokens[t++];
			}
			byte_buf[blen] = '\0';
			char folded[128];
			if (lemma_fold(folded, sizeof(folded), byte_buf) >= 0) {
				size_t flen = strlen(folded);
				if (flen >= sizeof(word_bufs[0]))
					flen = sizeof(word_bufs[0]) - 1;
				memcpy(word_bufs[n_words], folded, flen);
				word_bufs[n_words][flen] = '\0';
				word_lens[n_words] = flen;
				n_words++;
			}
		} else {
			t++;
		}
	}

	if (q->n > n_words)
		return 0;
	if (q->n == 1)
		return 1;

	for (size_t i = 0; i + q->n <= n_words; i++) {
		size_t j;
		for (j = 0; j < q->n; j++) {
			if (word_lens[i + j] < q->len[j])
				break;
			if (memcmp(word_bufs[i + j], q->toks[j], q->len[j]) != 0)
				break;
		}
		if (j == q->n)
			return 1;
	}
	return 0;
}

static uint32_t lemma_query_any(
        lemma_db_t *db, const char *field, const char *query, lemma_dest_t *out,
        int *handled, int phrase, int max_edit_distance)
{
	collect_ctx_t toks;
	char fold_stack[256];
	char *folded;
	char *prefix = NULL;
	char *dkey = NULL;
	size_t dcap = 0;
	size_t fld;
	size_t qlen;
	size_t pw = 0;
	unsigned cur_hd = 0;
	uint32_t matches = 0;
	size_t i;

	if (handled)
		*handled = 0;
	if (!db || !field || !query || !out)
		return 0;
	if (!out->hd && !out->set)
		return 0;
	/* The fold never grows its output, so strlen+1 always fits. Short
	 * queries fold into the stack buffer; long ones spill to the heap. */
	qlen = strlen(query);
	if (qlen + 1 <= sizeof(fold_stack)) {
		folded = fold_stack;
	} else {
		folded = malloc(qlen + 1);
		if (!folded)
			return 0;
	}
	if (lemma_fold(folded, qlen + 1, query) < 0) {
		if (folded != fold_stack)
			free(folded);
		return 0;
	}

	toks.n = 0;
	lemma_tokenize(folded, collect_token, &toks);
	if (toks.n == 0) {
		if (folded != fold_stack)
			free(folded);
		return 0;
	}
	if (handled)
		*handled = 1;

	fld = strlen(field);
	/* One prefix buffer for every token: the longest token cannot exceed
	 * the folded query, so a single sizing pass caps the allocation. */
	for (i = 0; i < toks.n; i++)
		if (toks.len[i] > pw)
			pw = toks.len[i];
	prefix = malloc(fld + pw + 2); /* field '\t' token NUL */
	if (!prefix) {
		if (folded != fold_stack)
			free(folded);
		return 0;
	}
	memcpy(prefix, field, fld);
	prefix[fld] = '\t';

	for (i = 0; i < toks.n; i++) {
		unsigned nxt_hd;
		size_t plen = fld + 1 + toks.len[i];
		uint32_t cur;
		const void *k;
		const void *v;

		memcpy(prefix + fld + 1, toks.toks[i], toks.len[i]);
		prefix[plen] = '\0';

		nxt_hd = corm_open(NULL, NULL, CM_STR, CM_STR, 0xFF, 0);
		if (!nxt_hd)
			break;

		if (max_edit_distance == 0) {
			/* Prefix scan over terms in term_hd: term_hd is CM_SORTED,
			 * CM_RANGE iterates from prefix to end; break when prefix
			 * no longer matches. */
			cur = corm_iter(db->term_hd, prefix, CM_RANGE);
			while (corm_next(&k, &v, cur)) {
				const char *tkey = (const char *)k;
				uint32_t tid;
				char post_prefix[10];
				uint32_t pcur;
				const void *pk;
				const void *pv;

				if (strncmp(tkey, prefix, plen) != 0)
					break;
				tid = *(const uint32_t *)v;
				snprintf(post_prefix, sizeof(post_prefix), "%08x\t", tid);

				pcur = corm_iter(db->hd, post_prefix, CM_RANGE);
				while (corm_next(&pk, &pv, pcur)) {
					const char *pkey = (const char *)pk;
					if (strncmp(pkey, post_prefix, 9) != 0)
						break;
					const char *rid = pkey + 9;
					if (!cur_hd || corm_get(cur_hd, rid))
						corm_put(nxt_hd, rid, "");
				}
				corm_fin(pcur);
			}
			corm_fin(cur);
		} else {
			/* Fuzzy scan over all terms for this field */
			char field_pfx[256];
			snprintf(field_pfx, sizeof(field_pfx), "%s\t", field);
			size_t fp_len = strlen(field_pfx);
			cur = corm_iter(db->term_hd, field_pfx, CM_RANGE);
			while (corm_next(&k, &v, cur)) {
				const char *tkey = (const char *)k;
				if (strncmp(tkey, field_pfx, fp_len) != 0)
					break;
				const char *tword = tkey + fp_len;
				size_t twlen = strlen(tword);
				int dist = lemma_levenshtein(toks.toks[i], toks.len[i], tword, twlen);
				if (dist <= max_edit_distance || (twlen >= toks.len[i] && strncmp(tword, toks.toks[i], toks.len[i]) == 0)) {
					uint32_t tid = *(const uint32_t *)v;
					char post_prefix[10];
					uint32_t pcur;
					const void *pk;
					const void *pv;

					snprintf(post_prefix, sizeof(post_prefix), "%08x\t", tid);
					pcur = corm_iter(db->hd, post_prefix, CM_RANGE);
					while (corm_next(&pk, &pv, pcur)) {
						const char *pkey = (const char *)pk;
						if (strncmp(pkey, post_prefix, 9) != 0)
							break;
						const char *rid = pkey + 9;
						if (!cur_hd || corm_get(cur_hd, rid))
							corm_put(nxt_hd, rid, "");
					}
					corm_fin(pcur);
				}
			}
			corm_fin(cur);
		}

		if (cur_hd)
			corm_close(cur_hd);
		cur_hd = nxt_hd;
	}

	if (cur_hd) {
		uint32_t cur = corm_iter(cur_hd, NULL, 0);
		const void *k;
		const void *v;

		while (corm_next(&k, &v, cur)) {
			const char *rid = (const char *)k;
			int keep = 1;

			if (phrase && toks.n > 1) {
				size_t rl = strlen(rid);
				size_t dfl = fld + 1 + rl;

				if (dfl + 1 > dcap) {
					char *nt = realloc(dkey, dfl + 1);
					if (!nt)
						break;
					dkey = nt;
					dcap = dfl + 1;
				}
				memcpy(dkey, field, fld);
				dkey[fld] = '\t';
				memcpy(dkey + fld + 1, rid, rl);
				dkey[dfl] = '\0';

				const void *raw = corm_get(db->doc_tok_hd, dkey);
				if (raw) {
					const uint16_t *hdr = (const uint16_t *)raw;
					keep = doc_tok_matches_phrase(hdr + 1, (size_t)hdr[0], &toks);
				} else {
					keep = 0;
				}
			}
			if (keep) {
				lemma_dest_emit(out, rid);
				matches++;
			}
		}
		corm_fin(cur);
		corm_close(cur_hd);
	}

	if (folded != fold_stack)
		free(folded);
	free(prefix);
	free(dkey);

	return matches;
}

uint32_t lemma_query(
        lemma_db_t *db, const char *field, const char *query, uint32_t out_hd,
        int *handled)
{
	lemma_dest_t d = { out_hd, NULL, 0 };

	return lemma_query_any(db, field, query, &d, handled, 0, 0);
}

uint32_t lemma_query_phrase(
        lemma_db_t *db, const char *field, const char *query, uint32_t out_hd,
        int *handled)
{
	lemma_dest_t d = { out_hd, NULL, 0 };

	return lemma_query_any(db, field, query, &d, handled, 1, 0);
}

uint32_t lemma_query_fuzzy(
        lemma_db_t *db, const char *field, const char *query, int max_edit_distance,
        uint32_t out_hd, int *handled)
{
	lemma_dest_t d = { out_hd, NULL, 0 };

	return lemma_query_any(db, field, query, &d, handled, 0, max_edit_distance);
}

/* ---- recall-kernel fill + rank ---- */

int rec_axis_fill_tokens(lemma_db_t *db, const char *field, const char *query,
                         int phrase, rec_set_t *out)
{
	lemma_dest_t d;

	if (!db || !field || !query || !out)
		return -1;
	d.hd = 0;
	d.set = out;
	d.err = 0;
	lemma_query_any(db, field, query, &d, NULL, phrase ? 1 : 0, 0);
	if (d.err)
		return -1;
	rec_set_seal(out);
	return 0;
}

int lemma_rank(struct lemma_rank_ctx *ctx, rec_ref_t ref, float *score)
{
	char rid[24];
	size_t fld;
	size_t rlen;
	char *dkey;
	const void *raw;

	if (!ctx || !ctx->db || !ctx->field || !score)
		return -1;
	snprintf(rid, sizeof(rid), "%llu", (unsigned long long)ref);
	fld = strlen(ctx->field);
	rlen = strlen(rid);
	dkey = malloc(fld + 1 + rlen + 1);
	if (!dkey)
		return -1;
	memcpy(dkey, ctx->field, fld);
	dkey[fld] = '\t';
	memcpy(dkey + fld + 1, rid, rlen);
	dkey[fld + 1 + rlen] = '\0';
	raw = corm_get(ctx->db->doc_tok_hd, dkey);
	free(dkey);
	if (!raw)
		return -1;

	const uint16_t *hdr = (const uint16_t *)raw;
	uint16_t n_tokens = hdr[0];
	const uint16_t *tokens = hdr + 1;
	const lemma_tok_vocab_t *vocab = lemma_tok_vocab_default();

	size_t word_count = 0;
	size_t t = 0;
	while (t < n_tokens) {
		uint16_t tid = tokens[t];
		if (vocab && lemma_tok_is_word(vocab, tid)) {
			word_count++;
			t++;
		} else if (tid < 256 && is_word_char((unsigned char)tid)) {
			word_count++;
			while (t < n_tokens && tokens[t] < 256 && is_word_char((unsigned char)tokens[t]))
				t++;
		} else {
			t++;
		}
	}
	if (word_count == 0)
		return -1;
	*score = (float)ctx->matched / (float)word_count;
	return 0;
}

/* ---- rec_query axis registration (lemma) ---- */

struct rec_lemma_params {
	const char *field;
	const char *query;
	int         phrase;
	size_t      matched; /* rank param: matched query tokens */
};

static int lemma_axis_fill(void *ctx, void *params, rec_set_t *out)
{
	lemma_db_t *db = ctx;
	const struct rec_lemma_params *p = params;

	if (!p)
		return -1;
	return rec_axis_fill_tokens(db, p->field, p->query, p->phrase, out);
}

static int lemma_axis_rank(void *ctx, void *params, rec_ref_t ref, float *score)
{
	const struct rec_lemma_params *p = params;
	struct lemma_rank_ctx sc;

	if (!p)
		return -1;
	sc.db = ctx;
	sc.field = p->field;
	sc.matched = p->matched;
	return lemma_rank(&sc, ref, score);
}

/*
 * Decode "field=body query='hello world' phrase=1 matched=2" into a
 * heap-owned rec_lemma_params. query/field default to "", phrase/
 * matched default to 0. Single-quoted values may contain spaces. The
 * decode-spec grammar is kernel-owned (ttypt/rec.h rec_spec_scan):
 * values are parsed in place with bounded parsers, so decode allocates
 * nothing until at most two owned string copies (field/query when the
 * leaf provides a non-empty value). The kept strings are owned copies;
 * CLI fallbacks are borrowed process-lifetime globals.
 */
static void *lemma_axis_decode(const char *s)
{
	struct rec_lemma_params *p;
	const char *q_leaf = NULL, *f_leaf = NULL;
	size_t qlen = 0, flen = 0;
	char *q_owned = NULL, *f_owned = NULL;
	int has_query = 0, has_field = 0, has_phrase = 0, has_matched = 0;
	int q_quoted = 0, f_quoted = 0;

	p = calloc(1, sizeof(*p));
	if (!p)
		return NULL;
	p->field = LEMMA_AXIS_TEXT_FIELD;
	p->query = "";
	p->phrase = 0;
	p->matched = 0;

	/* bare spec: searchable only when a CLI --query is present (field
	 * defaults to "text" — the documented store field, so there is no
	 * field="" hazard). Otherwise NULL (loud fill failure, unchanged). */
	if (!s) {
		if (!lemma_cli_query) {
			free(p);
			return NULL;
		}
		p->field = lemma_cli_field_set ? lemma_cli_field
		                              : LEMMA_AXIS_TEXT_FIELD;
		p->query = lemma_cli_query;
		p->phrase = lemma_cli_phrase;
		p->matched = lemma_cli_matched;
		return p;
	}

	/* empty spec stays a degenerate query (query="" -> no matches) */
	if (!*s)
		return p;

	{
		const char *cur = s, *key, *val;
		size_t klen, vlen;
		int quoted;

		while (rec_spec_scan(&cur, &key, &klen, &val, &vlen,
				     &quoted)) {
			if (rec_key_eq(key, klen, "field")) {
				f_leaf = val;
				flen = vlen;
				f_quoted = quoted;
				has_field = 1;
			} else if (rec_key_eq(key, klen, "query")) {
				q_leaf = val;
				qlen = vlen;
				q_quoted = quoted;
				has_query = 1;
			} else if (rec_key_eq(key, klen, "phrase")) {
				if (rec_cli_int_b(val, val + vlen,
						  &p->phrase) == 0)
					has_phrase = 1;
			} else if (rec_key_eq(key, klen, "matched")) {
				if (rec_cli_size_b(val, val + vlen,
						   &p->matched) == 0)
					has_matched = 1;
			}
		}
	}
	/* D14 CLI merge (round 2): fill the fields the leaf omitted. Leaf
	 * wins — even an empty leaf value (field= → the "text" default,
	 * exactly as before). */
	if (has_query) {
		if (rec_cli_str_dup(q_leaf, qlen, q_quoted,
				    &q_owned) != 0)
			goto fail;
		p->query = q_owned;
	} else if (lemma_cli_query) {
		p->query = lemma_cli_query;
	}
	if (has_field) {
		if (flen > 0) {
			if (rec_cli_str_dup(f_leaf, flen, f_quoted,
					    &f_owned) != 0)
				goto fail;
			p->field = f_owned;
		}                       /* empty leaf → keep "text" default */
	} else if (lemma_cli_field_set) {
		p->field = lemma_cli_field;
	}
	if (!has_phrase && lemma_cli_phrase_set)
		p->phrase = lemma_cli_phrase;
	if (!has_matched && lemma_cli_matched_set)
		p->matched = lemma_cli_matched;
	return p;

fail:
	free(q_owned);
	free(f_owned);
	free(p);
	return NULL;
}

__attribute__((constructor)) static void lemma_rec_axis_init(void)
{
	static const rec_axis_t lemma_axis = {
		"lemma", lemma_axis_fill, lemma_axis_rank, NULL, lemma_axis_decode
	};

	rec_axis_register(&lemma_axis);
}

/*
 * rec_axis_open convention (RECALL-KERNEL.md "rec_axis_open convention", optional CLI-open
 * convention, not part of libcorm's core rec_query registry API): spec
 * is the decimal corm hash mask for lemma_open() (empty/NULL -> 0, the
 * corm default) — or, when it names a path (contains a '/'), a
 * 2B-2 primary-seeded rebuild: lemma finds the primary corm store and
 * re-indexes every stored record (`:a:s` raw and `:a:u` decimal alike —
 * keys iterate as refs, values as text), emitting the one-line rebuild
 * budget note (mm-plan U4).
 *
 * Primary resolution (7-AXIS-NAMESPACE-PLAN.md B-1/B-2): the CLI publishes
 * the exact primary it opened as CORM_AXIS_PRIMARY, and lemma rebuilds
 * from THAT primary (verbatim → aliases the live handle) when its
 * `<primary>.roster` sidecar exists. Without the env it falls back to the
 * directory scan (exactly one `*.roster` → use it; none → loud warn, empty).
 * It NEVER guesses between sibling DBs: two or more rosters without the
 * env → loud warn, memory-only index (never silently empty, never the
 * wrong primary's data).
 *
 * Malformed/unfindable sidecar → loud warn, memory-only index. The
 * primary open uses the CLI's mask derivation (D11): CORM_MASK env
 * (validated 2^n-1) else the 4095 default — co-opened files must always
 * match. Returns the lemma_db_t* ctx directly (no cast needed).
 */
void *rec_axis_open(const char *spec)
{
	unsigned mask;

	if (spec && strchr(spec, '/')) {
		lemma_db_t *db = lemma_open(0);
		/* CLI mask derivation (D11): CORM_MASK overrides the 4095
		 * default — the sidecar rebuild must open the primary with
		 * the same table shape the CLI wrote. */
		const char *menv = getenv("CORM_MASK");
		const char *eprim = getenv("CORM_AXIS_PRIMARY");
		unsigned pmask = 256 - 1;
		char *tmp = strdup(spec);
		char *dir;
		char *primary = NULL;
		DIR *d;
		struct dirent *de;
		uint32_t hd;
		uint32_t cur;
		const void *key, *value;
		size_t docs = 0;
		struct timespec t0, t1;
		double ms;
		/* -1 = directory scan never ran (CORM_AXIS_PRIMARY used), else
		 * the count of *.roster sidecars the scan saw. */
		int nroster = -1;

		if (menv && *menv) {
			unsigned long v = strtoul(menv, NULL, 10);
			if (v != 0 && (v & (v + 1)) == 0)
				pmask = (unsigned) v;
		}

		if (!db) {
			free(tmp);
			return NULL;
		}
		if (tmp)
			dir = dirname(tmp);
		else
			dir = (char *)spec;

		/* B-1 (7-AXIS-NAMESPACE-PLAN.md): the CLI published the exact
		 * primary it opened. Rebuild from that verbatim path when its
		 * roster sidecar exists — it aliases the live handle and can
		 * never mis-pick a sibling DB. */
		if (eprim && *eprim) {
			char rs[BUFSIZ];
			struct stat st;

			snprintf(rs, sizeof(rs), "%s.roster", eprim);
			if (stat(rs, &st) == 0)
				primary = strdup(eprim);
			else
				fprintf(stderr,
					"lemma: CORM_AXIS_PRIMARY '%s' has no "
					"'%s'; falling back to the directory "
					"scan\n",
					eprim, rs);
		}

		if (!primary) {
			nroster = 0;

			d = opendir(dir);
			if (d) {
				while ((de = readdir(d))) {
					static const char suf[] = ".roster";
					size_t n = strlen(de->d_name);
					size_t sl = sizeof(suf) - 1;

					if (n <= sl ||
							strcmp(de->d_name + n - sl,
								suf) != 0)
						continue;
					nroster++;
					if (nroster == 1) {
						size_t need = strlen(dir) + 1
							+ (n - sl) + 1;
						primary = malloc(need);
						if (primary)
							snprintf(primary, need,
								"%s/%.*s", dir,
								(int)(n - sl),
								de->d_name);
					}
				}
				closedir(d);
			}

			/* B-2: never guess between sibling DBs. Zero rosters =
			 * no primary (existing warn); two+ without the env =
			 * loud warn, stay empty (not the wrong primary). */
			if (nroster > 1) {
				fprintf(stderr,
					"lemma: %d roster sidecars in '%s'; "
					"cannot pick a primary (set "
					"CORM_AXIS_PRIMARY to one DB); text "
					"queries stay empty\n",
					nroster, dir);
				free(primary);
				primary = NULL;
			}
		}

		if (!primary && nroster == 0) {
			fprintf(stderr,
				"lemma: no primary found in '%s' (no *.roster "
				"sidecar); text queries stay empty\n",
				dir);
		} else {
			const char *vt = getenv("CORM_AXIS_VTYPE");
			int is_blob = (vt && *vt == 'b');
			uint32_t vtype = is_blob ? corm_blob_type() : CM_STR;

			hd = corm_open(primary, "hd", CM_HNDL, vtype,
					pmask, CM_AINDEX | CM_MIRROR);
			if (!hd) {
				fprintf(stderr,
					"lemma: cannot open primary '%s'\n",
					primary);
			} else {
				clock_gettime(CLOCK_MONOTONIC, &t0);
				cur = corm_iter(hd, NULL, 0);
				while (corm_next(&key, &value, cur)) {
					rec_ref_t ref = 0;
					memcpy(&ref, key,
							corm_type_len(CM_HNDL));
					if (value) {
						if (is_blob) {
							uint32_t blen = *(const uint32_t *)value;
							const void *bdata = (const char *)value + sizeof(uint32_t);
							lemma_store_typed(db, NULL, ref, bdata, blen, vtype);
						} else {
							lemma_index_ref(db,
								LEMMA_AXIS_TEXT_FIELD,
								ref, (const char *)value);
						}
					}
					docs++;
				}
				corm_fin(cur);
				clock_gettime(CLOCK_MONOTONIC, &t1);
				ms = (double)(t1.tv_sec - t0.tv_sec) * 1000.0
					+ (double)(t1.tv_nsec - t0.tv_nsec)
					/ 1000000.0;
				fprintf(stderr, "lemma: rebuilt %zu docs in "
						"%.2f ms\n", docs, ms);
			}
		}
		free(primary);
		free(tmp);
		return db;
	}

	mask = (spec && *spec) ? (unsigned)strtoul(spec, NULL, 10) : 0;
	return lemma_open(mask);
}

/*
 * rec_axis_cli_options / rec_axis_config_arg conventions (D14, round 2:
 * optional axis-contributed CLI options — not libcorm core API): corm
 * collects inline `--name=value` tokens and, once every bound axis is
 * connected, broadcasts each to the declarations via these dlsym'd symbols.
 * lemma declares `query` (full-text query), `field` (query field), `phrase`
 * and `matched`; decode merges them into the bare/empty/leaf paths (leaf
 * spec wins; field defaults to "text"). The option struct ABI is
 * kernel-owned in <ttypt/rec.h>.
 */
const struct rec_axis_cli_option *
rec_axis_cli_options(void)
{
	static const struct rec_axis_cli_option opts[] = {
		{ "query", 1, "full-text query" },
		{ "field", 1, "text field to query" },
		{ "phrase", 1, "phrase match 0|1" },
		{ "matched", 1, "rank matched-token numerator" },
		{ NULL, 0, NULL }
	};
	return opts;
}

int
rec_axis_config_arg(const char *name, const char *value)
{
	int iv;
	size_t nv;

	if (!name || !value)
		return -1;
	if (!strcmp(name, "query"))
		return rec_cli_str_set(&lemma_cli_query, value);
	if (!strcmp(name, "field")) {
		if (rec_cli_str_set(&lemma_cli_field, value) != 0)
			return -1;
		lemma_cli_field_set = 1;
		return 0;
	}
	if (!strcmp(name, "phrase")) {
		if (rec_cli_int(value, &iv) != 0 || (iv != 0 && iv != 1))
			return -1;
		lemma_cli_phrase = iv;
		lemma_cli_phrase_set = 1;
		return 0;
	}
	if (!strcmp(name, "matched")) {
		if (rec_cli_size(value, &nv) != 0)
			return -1;
		lemma_cli_matched = nv;
		lemma_cli_matched_set = 1;
		return 0;
	}
	return -1;
}
