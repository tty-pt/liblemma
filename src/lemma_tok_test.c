#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ttypt/corm.h>
#include "ttypt/lemma.h"

static int failures = 0;
static int total = 0;

#define CHECK(cond, name)                                                      \
	do {                                                                   \
		total++;                                                       \
		if (!(cond)) {                                                 \
			failures++;                                            \
			printf("FAIL: %s (line %d)\n", name, __LINE__);        \
		}                                                              \
	} while (0)

static void test_roundtrip(const char *label, const char *input)
{
	const lemma_tok_vocab_t *vocab = lemma_tok_vocab_default();
	CHECK(vocab != NULL, "vocab default available");
	if (!vocab)
		return;

	size_t in_len = strlen(input);
	uint16_t tokens[4096];
	size_t n_tokens = 0;

	int rc_enc = lemma_tok_encode(vocab, input, in_len, tokens, 4096, &n_tokens);
	CHECK(rc_enc == 0, label);

	char decoded[8192];
	size_t out_len = 0;
	int rc_dec = lemma_tok_decode(vocab, tokens, n_tokens, decoded, sizeof(decoded), &out_len);
	CHECK(rc_dec == 0, label);
	CHECK(out_len == in_len, label);
	CHECK(strcmp(decoded, input) == 0, label);
}

static void test_roundtrip_binary(const char *label, const char *input, size_t in_len)
{
	const lemma_tok_vocab_t *vocab = lemma_tok_vocab_default();
	CHECK(vocab != NULL, "vocab default available");
	if (!vocab)
		return;

	uint16_t tokens[4096];
	size_t n_tokens = 0;

	int rc_enc = lemma_tok_encode(vocab, input, in_len, tokens, 4096, &n_tokens);
	CHECK(rc_enc == 0, label);

	char decoded[8192];
	size_t out_len = 0;
	int rc_dec = lemma_tok_decode(vocab, tokens, n_tokens, decoded, sizeof(decoded), &out_len);
	CHECK(rc_dec == 0, label);
	CHECK(out_len == in_len, label);
	CHECK(memcmp(decoded, input, in_len) == 0, label);
}

int main(void)
{
	printf("=== lemma_tok tests ===\n");

	/* Basic roundtrip */
	test_roundtrip("empty string", "");
	test_roundtrip("single char", "a");
	test_roundtrip("simple words", "hello world");
	test_roundtrip("punctuation and symbols", "void foo(int x, char **argv) { return x + 1; }");
	test_roundtrip("multiline whitespace", "\t\tline 1\r\n\n  line 2   \n");
	test_roundtrip("accents portuguese", "o pão de cada dia, coração e música");
	test_roundtrip("mixed code and comments", "/* calculate checksum */\nuint32_t c = axil_checksum(data, len);\n");

	/* Binary / escape roundtrip */
	const char bin_data[] = { 'h', 'i', 0x00, 0x01, (char)0xFF, (char)0xFE, 'b', 'y', 'e' };
	test_roundtrip_binary("binary with zero byte", bin_data, sizeof(bin_data));

	/* Word vs delimiter classification */
	{
		const lemma_tok_vocab_t *v = lemma_tok_vocab_default();
		uint16_t toks[16];
		size_t n = 0;
		lemma_tok_encode(v, "hello world", 11, toks, 16, &n);
		CHECK(n >= 3, "hello space world has >= 3 tokens");
		if (n >= 3) {
			CHECK(lemma_tok_is_word(v, toks[0]) == 1, "hello is word");
			CHECK(lemma_tok_is_word(v, toks[1]) == 0, "space is not word");
			CHECK(lemma_tok_is_word(v, toks[2]) == 1, "world is word");
		}
	}

	/* Dynamic OOV interning */
	{
		lemma_tok_vocab_t *dyn = lemma_tok_vocab_new(NULL);
		CHECK(dyn != NULL, "dyn vocab created");
		if (dyn) {
			const char *novel = "Supercalifragilisticexpialidocious_12345";
			uint16_t toks[32];
			size_t n = 0;
			int rc = lemma_tok_encode(dyn, novel, strlen(novel), toks, 32, &n);
			CHECK(rc == 0, "dyn encode novel word");

			char dec[128];
			size_t dlen = 0;
			rc = lemma_tok_decode(dyn, toks, n, dec, sizeof(dec), &dlen);
			CHECK(rc == 0, "dyn decode novel word");
			CHECK(strcmp(dec, novel) == 0, "dyn roundtrip matches");
			lemma_tok_vocab_free(dyn);
		}
	}

	/* Tier 1 Fuzzy Vocab Matching */
	{
		const lemma_tok_vocab_t *v = lemma_tok_vocab_default();
		uint16_t matches[32];
		size_t n_matches = 0;

		/* Exact and prefix match */
		int rc = lemma_tok_vocab_match(v, "calculate", 0, matches, 32, &n_matches);
		CHECK(rc == 0, "match exact calculate");
		CHECK(n_matches >= 1, "found calculate");

		/* Typo with distance 1: "calclate" -> "calculate" */
		n_matches = 0;
		rc = lemma_tok_vocab_match(v, "calclate", 1, matches, 32, &n_matches);
		CHECK(rc == 0, "fuzzy match calclate");
		CHECK(n_matches >= 1, "found typo match calculate");

		/* Prefix / Substring */
		n_matches = 0;
		rc = lemma_tok_vocab_match(v, "port", 0, matches, 32, &n_matches);
		CHECK(rc == 0, "match prefix port");
		CHECK(n_matches >= 1, "found tokens matching port");
	}

	/* Tier 2: Token-stream Indexing & Phrase / Fuzzy Query */
	{
		lemma_db_t *db = lemma_open(0);
		CHECK(db != NULL, "lemma_open ok");
		if (db) {
			const lemma_tok_vocab_t *v = lemma_tok_vocab_default();
			uint16_t toks1[64], toks2[64];
			size_t n1 = 0, n2 = 0;

			lemma_tok_encode(v, "hello world from token stream", 29, toks1, 64, &n1);
			lemma_tok_encode(v, "world hello another calculate example", 37, toks2, 64, &n2);

			int rc1 = lemma_index_tokens(db, "text", "row1", toks1, n1);
			int rc2 = lemma_index_tokens(db, "text", "row2", toks2, n2);
			CHECK(rc1 == 0, "index toks1 ok");
			CHECK(rc2 == 0, "index toks2 ok");

			/* Standard query */
			unsigned out1 = corm_open(NULL, NULL, CM_STR, CM_STR, 0xFF, 0);
			int handled = 0;
			uint32_t hits = lemma_query(db, "text", "hello", out1, &handled);
			CHECK(handled == 1, "query handled");
			CHECK(hits == 2, "query hello hits 2 rows");
			CHECK(corm_get(out1, "row1") != NULL, "row1 matches hello");
			CHECK(corm_get(out1, "row2") != NULL, "row2 matches hello");
			corm_close(out1);

			/* Phrase query over token stream */
			unsigned out2 = corm_open(NULL, NULL, CM_STR, CM_STR, 0xFF, 0);
			handled = 0;
			hits = lemma_query_phrase(db, "text", "hello world", out2, &handled);
			CHECK(handled == 1, "phrase handled");
			CHECK(hits == 1, "phrase 'hello world' matches 1 row");
			CHECK(corm_get(out2, "row1") != NULL, "row1 matches phrase 'hello world'");
			CHECK(corm_get(out2, "row2") == NULL, "row2 does NOT match phrase 'hello world'");
			corm_close(out2);

			/* Fuzzy query with edit distance 1: "calclate" -> "calculate" */
			unsigned out3 = corm_open(NULL, NULL, CM_STR, CM_STR, 0xFF, 0);
			handled = 0;
			hits = lemma_query_fuzzy(db, "text", "calclate", 1, out3, &handled);
			CHECK(handled == 1, "fuzzy query handled");
			CHECK(hits == 1, "fuzzy query 'calclate' hits 1 row");
			CHECK(corm_get(out3, "row2") != NULL, "row2 matches fuzzy 'calclate'");
			corm_close(out3);

			/* Accent-sensitive query invariant: "pao" vs "pão" */
			uint16_t toks3[32];
			size_t n3 = 0;
			lemma_tok_encode(v, "o pão quente", 13, toks3, 32, &n3);
			lemma_index_tokens(db, "text", "row3", toks3, n3);

			unsigned out4 = corm_open(NULL, NULL, CM_STR, CM_STR, 0xFF, 0);
			handled = 0;
			hits = lemma_query(db, "text", "pao", out4, &handled);
			CHECK(hits == 0, "unaccented pao does NOT match pão");
			corm_close(out4);

			unsigned out5 = corm_open(NULL, NULL, CM_STR, CM_STR, 0xFF, 0);
			handled = 0;
			hits = lemma_query(db, "text", "pão", out5, &handled);
			CHECK(hits == 1, "accented pão matches pão");
			CHECK(corm_get(out5, "row3") != NULL, "row3 matches pão");
			corm_close(out5);

			lemma_close(db);
		}
	}

	printf("-----------------------\n");
	printf("Results: %d/%d passed\n", total - failures, total);
	return failures ? 1 : 0;
}
