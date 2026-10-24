#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "ttypt/lemma.h"

#define VOCAB_HASH_BITS 16
#define VOCAB_HASH_SIZE (1 << VOCAB_HASH_BITS)
#define VOCAB_HASH_MASK (VOCAB_HASH_SIZE - 1)

#define QGRAM_BUCKETS 4096
#define QGRAM_MASK (QGRAM_BUCKETS - 1)

typedef struct vocab_entry {
	char *str;
	uint16_t len;
	uint16_t token_id;
	uint8_t is_word;
	struct vocab_entry *next;
} vocab_entry_t;

typedef struct qgram_entry {
	uint32_t trigram;
	uint16_t token_id;
	struct qgram_entry *next;
} qgram_entry_t;

struct lemma_tok_vocab {
	uint16_t vocab_size;
	uint16_t max_vocab;
	int is_dynamic;

	/* Direct array indexing for O(1) decoding */
	char *token_str[LEMMA_TOK_MAX_VOCAB];
	uint8_t token_len[LEMMA_TOK_MAX_VOCAB];
	uint8_t is_word[LEMMA_TOK_MAX_VOCAB];

	/* Fast hash lookup for encoding: string -> token_id */
	vocab_entry_t *buckets[VOCAB_HASH_SIZE];

	/* Character 3-gram index over vocabulary words (Tier 1 search acceleration) */
	qgram_entry_t *qgram_buckets[QGRAM_BUCKETS];

	/* Byte primitives backing buffer */
	char byte_chars[256];
};

static uint32_t str_hash(const char *str, size_t len)
{
	uint32_t h = 2166136261u;
	for (size_t i = 0; i < len; i++) {
		h ^= (uint8_t)str[i];
		h *= 16777619u;
	}
	return h;
}

static int is_word_char(unsigned char c)
{
	return isalnum(c) || c >= 0x80;
}

static void vocab_index_qgrams(lemma_tok_vocab_t *v, uint16_t token_id, const char *str, size_t len)
{
	(void)len;
	char folded[128];
	if (lemma_fold(folded, sizeof(folded), str) < 0)
		return;
	size_t flen = strlen(folded);
	if (flen < 3)
		return;

	for (size_t i = 0; i + 3 <= flen; i++) {
		uint32_t tg = ((uint8_t)folded[i] << 16) | ((uint8_t)folded[i+1] << 8) | (uint8_t)folded[i+2];
		uint32_t b = tg & QGRAM_MASK;

		int exists = 0;
		for (qgram_entry_t *qe = v->qgram_buckets[b]; qe; qe = qe->next) {
			if (qe->trigram == tg && qe->token_id == token_id) {
				exists = 1;
				break;
			}
		}
		if (!exists) {
			qgram_entry_t *qe = malloc(sizeof(*qe));
			if (qe) {
				qe->trigram = tg;
				qe->token_id = token_id;
				qe->next = v->qgram_buckets[b];
				v->qgram_buckets[b] = qe;
			}
		}
	}
}

static int vocab_insert_token(lemma_tok_vocab_t *v, uint16_t id, const char *str, size_t len, int is_word)
{
	if (id >= LEMMA_TOK_MAX_VOCAB || !v || !str)
		return -1;

	char *copy = malloc(len + 1);
	if (!copy)
		return -1;
	memcpy(copy, str, len);
	copy[len] = '\0';

	v->token_str[id] = copy;
	v->token_len[id] = (uint8_t)(len > 255 ? 255 : len);
	v->is_word[id] = (uint8_t)(is_word ? 1 : 0);

	uint32_t b = str_hash(str, len) & VOCAB_HASH_MASK;
	vocab_entry_t *ent = malloc(sizeof(*ent));
	if (!ent) {
		free(copy);
		return -1;
	}
	ent->str = copy;
	ent->len = (uint16_t)len;
	ent->token_id = id;
	ent->is_word = (uint8_t)(is_word ? 1 : 0);
	ent->next = v->buckets[b];
	v->buckets[b] = ent;

	if (is_word)
		vocab_index_qgrams(v, id, str, len);

	if (id >= v->vocab_size)
		v->vocab_size = (uint16_t)(id + 1);
	return 0;
}

#include "vocab_base.h"

static const char *BASE_DELIMS[] = {
	"  ", "    ", "        ", "\r\n", "\n\n", "/*", "*/", "->", "::",
	"==", "!=", "<=", ">=", "&&", "||", "//", "],", "},", "';", "\",",
	": ", ", ", " - ", " = ", " — ", " – ", " -- ", " | ", " / ",
	NULL
};

static void init_vocab_tables(lemma_tok_vocab_t *v)
{
	/* 1. Byte primitives (0..255) */
	for (int i = 0; i < 256; i++) {
		v->byte_chars[i] = (char)i;
		v->token_str[i] = &v->byte_chars[i];
		v->token_len[i] = 1;
		v->is_word[i] = 0;

		uint32_t b = str_hash(&v->byte_chars[i], 1) & VOCAB_HASH_MASK;
		vocab_entry_t *ent = malloc(sizeof(*ent));
		if (ent) {
			ent->str = &v->byte_chars[i];
			ent->len = 1;
			ent->token_id = (uint16_t)i;
			ent->is_word = 0;
			ent->next = v->buckets[b];
			v->buckets[b] = ent;
		}
	}
	v->vocab_size = 256;

	/* 2. Common delimiters starting at 256 */
	uint16_t id = 256;
	for (size_t i = 0; BASE_DELIMS[i] && id < 512; i++) {
		vocab_insert_token(v, id++, BASE_DELIMS[i], strlen(BASE_DELIMS[i]), 0);
	}

	/* 3. Base static words starting at 512 */
	id = 512;
	for (size_t i = 0; BASE_WORDS[i] && id < LEMMA_TOK_BASE_VOCAB; i++) {
		vocab_insert_token(v, id++, BASE_WORDS[i], strlen(BASE_WORDS[i]), 1);
	}
}

static lemma_tok_vocab_t *g_default_vocab = NULL;

const lemma_tok_vocab_t *lemma_tok_vocab_default(void)
{
	if (!g_default_vocab) {
		g_default_vocab = calloc(1, sizeof(*g_default_vocab));
		if (g_default_vocab) {
			g_default_vocab->max_vocab = LEMMA_TOK_MAX_VOCAB;
			g_default_vocab->is_dynamic = 0;
			init_vocab_tables(g_default_vocab);
		}
	}
	return g_default_vocab;
}

lemma_tok_vocab_t *lemma_tok_vocab_new(const lemma_tok_vocab_t *base)
{
	lemma_tok_vocab_t *v = calloc(1, sizeof(*v));
	if (!v)
		return NULL;
	v->max_vocab = LEMMA_TOK_MAX_VOCAB;
	v->is_dynamic = 1;

	const lemma_tok_vocab_t *src = base ? base : lemma_tok_vocab_default();
	if (src) {
		/* Copy base vocabulary tokens */
		for (int i = 0; i < 256; i++) {
			v->byte_chars[i] = (char)i;
			v->token_str[i] = &v->byte_chars[i];
			v->token_len[i] = 1;
			v->is_word[i] = 0;
			uint32_t b = str_hash(&v->byte_chars[i], 1) & VOCAB_HASH_MASK;
			vocab_entry_t *ent = malloc(sizeof(*ent));
			if (ent) {
				ent->str = &v->byte_chars[i];
				ent->len = 1;
				ent->token_id = (uint16_t)i;
				ent->is_word = 0;
				ent->next = v->buckets[b];
				v->buckets[b] = ent;
			}
		}
		v->vocab_size = src->vocab_size;
		for (uint16_t i = 256; i < src->vocab_size; i++) {
			if (src->token_str[i]) {
				vocab_insert_token(v, i, src->token_str[i], src->token_len[i], src->is_word[i]);
			}
		}
	} else {
		init_vocab_tables(v);
	}
	return v;
}

void lemma_tok_vocab_free(lemma_tok_vocab_t *v)
{
	if (!v || v == g_default_vocab)
		return;

	for (size_t b = 0; b < VOCAB_HASH_SIZE; b++) {
		vocab_entry_t *e = v->buckets[b];
		while (e) {
			vocab_entry_t *nxt = e->next;
			free(e);
			e = nxt;
		}
	}
	for (size_t b = 0; b < QGRAM_BUCKETS; b++) {
		qgram_entry_t *qe = v->qgram_buckets[b];
		while (qe) {
			qgram_entry_t *nxt = qe->next;
			free(qe);
			qe = nxt;
		}
	}
	for (uint16_t i = 256; i < v->vocab_size; i++) {
		if (v->token_str[i])
			free(v->token_str[i]);
	}
	free(v);
}

int lemma_tok_vocab_load_file(lemma_tok_vocab_t *v, const char *filepath)
{
	FILE *f;
	char line[256];

	if (!v || !v->is_dynamic || !filepath)
		return -1;

	f = fopen(filepath, "r");
	if (!f)
		return -1;

	while (fgets(line, sizeof(line), f)) {
		char *p = line;
		while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
			p++;
		char *end = p + strlen(p);
		while (end > p && (*(end - 1) == ' ' || *(end - 1) == '\t' || *(end - 1) == '\r' || *(end - 1) == '\n'))
			end--;
		*end = '\0';
		if (*p && v->vocab_size < v->max_vocab) {
			size_t len = (size_t)(end - p);
			vocab_insert_token(v, v->vocab_size++, p, len, 1);
		}
	}
	fclose(f);
	return 0;
}

static uint16_t vocab_lookup(const lemma_tok_vocab_t *v, const char *str, size_t len)
{
	uint32_t b = str_hash(str, len) & VOCAB_HASH_MASK;
	for (vocab_entry_t *e = v->buckets[b]; e; e = e->next) {
		if (e->len == len && memcmp(e->str, str, len) == 0)
			return e->token_id;
	}
	return 0xFFFF;
}

static uint16_t vocab_intern(lemma_tok_vocab_t *v, const char *str, size_t len, int is_word)
{
	uint16_t existing = vocab_lookup(v, str, len);
	if (existing != 0xFFFF)
		return existing;

	if (v->vocab_size >= v->max_vocab || !v->is_dynamic)
		return 0xFFFF;

	uint16_t new_id = v->vocab_size;
	if (vocab_insert_token(v, new_id, str, len, is_word) == 0)
		return new_id;
	return 0xFFFF;
}

int lemma_tok_is_word(const lemma_tok_vocab_t *vocab, uint16_t token_id)
{
	const lemma_tok_vocab_t *v = vocab ? vocab : lemma_tok_vocab_default();
	if (!v || token_id >= v->vocab_size)
		return 0;
	return v->is_word[token_id];
}

int lemma_tok_encode(const lemma_tok_vocab_t *vocab, const char *src, size_t src_len,
                     uint16_t *dst, size_t dst_cap, size_t *dst_len)
{
	if (!src || !dst || !dst_len)
		return -1;

	lemma_tok_vocab_t *v = (lemma_tok_vocab_t *)(vocab ? vocab : lemma_tok_vocab_default());
	if (!v)
		return -1;

	size_t n_out = 0;
	const char *p = src;
	const char *end = src + src_len;

	while (p < end) {
		unsigned char c = (unsigned char)*p;

		if (is_word_char(c)) {
			/* Maximal run of word characters */
			const char *wstart = p;
			while (p < end && is_word_char((unsigned char)*p))
				p++;
			size_t wlen = (size_t)(p - wstart);

			uint16_t tid = vocab_lookup(v, wstart, wlen);
			if (tid == 0xFFFF && v->is_dynamic)
				tid = vocab_intern(v, wstart, wlen, 1);

			if (tid != 0xFFFF) {
				if (n_out >= dst_cap)
					return -1;
				dst[n_out++] = tid;
			} else {
				/* Byte primitive fallback for unknown word */
				if (n_out + wlen > dst_cap)
					return -1;
				for (size_t i = 0; i < wlen; i++)
					dst[n_out++] = (uint8_t)wstart[i];
			}
		} else {
			/* Delimiter: check multi-char delimiter first */
			int matched_delim = 0;
			if (p + 1 < end) {
				size_t check_len = (size_t)(end - p);
				if (check_len > 8)
					check_len = 8;
				for (size_t l = check_len; l >= 2; l--) {
					uint16_t tid = vocab_lookup(v, p, l);
					if (tid != 0xFFFF && !v->is_word[tid]) {
						if (n_out >= dst_cap)
							return -1;
						dst[n_out++] = tid;
						p += l;
						matched_delim = 1;
						break;
					}
				}
			}
			if (!matched_delim) {
				/* Single byte delimiter */
				if (n_out >= dst_cap)
					return -1;
				dst[n_out++] = (uint8_t)*p++;
			}
		}
	}

	*dst_len = n_out;
	return 0;
}

int lemma_tok_decode(const lemma_tok_vocab_t *vocab, const uint16_t *tokens, size_t n_tokens,
                     char *dst, size_t dst_cap, size_t *bytes_written)
{
	if (!tokens || !dst)
		return -1;

	const lemma_tok_vocab_t *v = vocab ? vocab : lemma_tok_vocab_default();
	if (!v)
		return -1;

	size_t pos = 0;
	for (size_t i = 0; i < n_tokens; i++) {
		uint16_t tid = tokens[i];
		if (tid >= v->vocab_size || !v->token_str[tid])
			return -1; /* Corrupted token ID */

		uint8_t len = v->token_len[tid];
		if (pos + len >= dst_cap)
			return -2; /* Buffer overflow */

		memcpy(dst + pos, v->token_str[tid], len);
		pos += len;
	}

	dst[pos] = '\0';
	if (bytes_written)
		*bytes_written = pos;
	return 0;
}

/* Levenshtein distance on folded strings */
int lemma_levenshtein(const char *s1, size_t l1, const char *s2, size_t l2)
{
	if (l1 == 0) return (int)l2;
	if (l2 == 0) return (int)l1;

	int d[33][33];
	if (l1 > 32) l1 = 32;
	if (l2 > 32) l2 = 32;

	for (size_t i = 0; i <= l1; i++) d[i][0] = (int)i;
	for (size_t j = 0; j <= l2; j++) d[0][j] = (int)j;

	for (size_t i = 1; i <= l1; i++) {
		for (size_t j = 1; j <= l2; j++) {
			int cost = (s1[i - 1] == s2[j - 1]) ? 0 : 1;
			int del = d[i - 1][j] + 1;
			int ins = d[i][j - 1] + 1;
			int sub = d[i - 1][j - 1] + cost;
			int min = del < ins ? del : ins;
			d[i][j] = min < sub ? min : sub;
		}
	}
	return d[l1][l2];
}

int lemma_tok_vocab_match(const lemma_tok_vocab_t *vocab, const char *term, int max_edit_distance,
                          uint16_t *out_token_ids, size_t out_cap, size_t *out_count)
{
	if (!term || !out_token_ids || !out_count)
		return -1;

	const lemma_tok_vocab_t *v = vocab ? vocab : lemma_tok_vocab_default();
	if (!v)
		return -1;

	char folded_query[128];
	if (lemma_fold(folded_query, sizeof(folded_query), term) < 0)
		return -1;
	size_t qlen = strlen(folded_query);
	if (qlen == 0) {
		*out_count = 0;
		return 0;
	}

	size_t matches = 0;

	if (qlen >= 3) {
		uint8_t checked[LEMMA_TOK_MAX_VOCAB] = { 0 };

		for (size_t i = 0; i + 3 <= qlen; i++) {
			uint32_t tg = ((uint8_t)folded_query[i] << 16) | ((uint8_t)folded_query[i+1] << 8) | (uint8_t)folded_query[i+2];
			uint32_t b = tg & QGRAM_MASK;

			for (qgram_entry_t *qe = v->qgram_buckets[b]; qe; qe = qe->next) {
				if (qe->trigram != tg)
					continue;
				uint16_t tid = qe->token_id;
				if (checked[tid])
					continue;
				checked[tid] = 1;

				if (!v->is_word[tid] || !v->token_str[tid])
					continue;

				char folded_word[128];
				if (lemma_fold(folded_word, sizeof(folded_word), v->token_str[tid]) < 0)
					continue;
				size_t wlen = strlen(folded_word);

				if (max_edit_distance == 0) {
					if (strstr(folded_word, folded_query) != NULL) {
						if (matches < out_cap)
							out_token_ids[matches++] = tid;
					}
				} else {
					int dist = lemma_levenshtein(folded_query, qlen, folded_word, wlen);
					if (dist <= max_edit_distance) {
						if (matches < out_cap)
							out_token_ids[matches++] = tid;
					}
				}
			}
		}
	} else {
		for (uint16_t tid = 256; tid < v->vocab_size; tid++) {
			if (!v->is_word[tid] || !v->token_str[tid])
				continue;

			char folded_word[128];
			if (lemma_fold(folded_word, sizeof(folded_word), v->token_str[tid]) < 0)
				continue;
			size_t wlen = strlen(folded_word);

			if (max_edit_distance == 0) {
				if (strstr(folded_word, folded_query) != NULL) {
					if (matches < out_cap)
						out_token_ids[matches++] = tid;
				}
			} else {
				int dist = lemma_levenshtein(folded_query, qlen, folded_word, wlen);
				if (dist <= max_edit_distance) {
					if (matches < out_cap)
						out_token_ids[matches++] = tid;
				}
			}
		}
	}

	*out_count = matches;
	return 0;
}

size_t lemma_tok_unpack_varint(const uint8_t *src, size_t src_len, uint16_t *dst, size_t dst_cap)
{
	size_t s = 0, d = 0;
	if (!src || !dst || dst_cap == 0) return 0;
	while (s < src_len && d < dst_cap) {
		uint8_t b = src[s++];
		if (!(b & 0x80)) {
			dst[d++] = (uint16_t)b;
		} else {
			if (s >= src_len) break;
			uint8_t b2 = src[s++];
			dst[d++] = (uint16_t)(b & 0x7f) | ((uint16_t)b2 << 7);
		}
	}
	return d;
}
