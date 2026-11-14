#include <node_api.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <ttypt/lemma.h>
#include <ttypt/cormr.h>

typedef struct {
    lemma_db_t *db;
    bool is_open;
} LemmaDbWrapper;

static void LemmaDbFinalizer(napi_env env, void *finalize_data, void *finalize_hint) {
    (void)env;
    (void)finalize_hint;
    LemmaDbWrapper *wrap = (LemmaDbWrapper *)finalize_data;
    if (wrap) {
        if (wrap->is_open && wrap->db) {
            lemma_close(wrap->db);
            wrap->db = NULL;
            wrap->is_open = false;
        }
        free(wrap);
    }
}

/* lemmaFold(inputString) -> foldedString */
static napi_value LemmaFold(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);

    if (argc < 1) {
        napi_throw_type_error(env, NULL, "Expected 1 argument: (inputString)");
        return NULL;
    }

    size_t in_len = 0;
    napi_get_value_string_utf8(env, args[0], NULL, 0, &in_len);

    char *in_buf = (char *)malloc(in_len + 1);
    if (!in_buf) {
        napi_throw_error(env, NULL, "Out of memory");
        return NULL;
    }
    napi_get_value_string_utf8(env, args[0], in_buf, in_len + 1, &in_len);

    size_t out_cap = in_len * 4 + 32;
    char *out_buf = (char *)malloc(out_cap);
    if (!out_buf) {
        free(in_buf);
        napi_throw_error(env, NULL, "Out of memory");
        return NULL;
    }

    int written = lemma_fold(out_buf, out_cap, in_buf);
    if (written < 0) {
        strncpy(out_buf, in_buf, out_cap - 1);
        out_buf[out_cap - 1] = '\0';
    }

    napi_value res;
    napi_create_string_utf8(env, out_buf, NAPI_AUTO_LENGTH, &res);

    free(in_buf);
    free(out_buf);
    return res;
}

typedef struct {
    napi_env env;
    napi_value arr;
    uint32_t count;
} TokCollectCtx;

static void tok_collect_cb(const char *tok, size_t len, void *user) {
    TokCollectCtx *ctx = (TokCollectCtx *)user;
    napi_value str_val;
    napi_create_string_utf8(ctx->env, tok, len, &str_val);
    napi_set_element(ctx->env, ctx->arr, ctx->count++, str_val);
}

/* lemmaTokenize(inputString) -> string[] */
static napi_value LemmaTokenize(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);

    if (argc < 1) {
        napi_throw_type_error(env, NULL, "Expected 1 argument: (inputString)");
        return NULL;
    }

    size_t in_len = 0;
    napi_get_value_string_utf8(env, args[0], NULL, 0, &in_len);

    char *in_buf = (char *)malloc(in_len + 1);
    if (!in_buf) {
        napi_throw_error(env, NULL, "Out of memory");
        return NULL;
    }
    napi_get_value_string_utf8(env, args[0], in_buf, in_len + 1, &in_len);

    size_t fold_cap = in_len * 4 + 32;
    char *fold_buf = (char *)malloc(fold_cap);
    if (!fold_buf) {
        free(in_buf);
        napi_throw_error(env, NULL, "Out of memory");
        return NULL;
    }

    if (lemma_fold(fold_buf, fold_cap, in_buf) < 0) {
        strncpy(fold_buf, in_buf, fold_cap - 1);
        fold_buf[fold_cap - 1] = '\0';
    }

    napi_value arr;
    napi_create_array(env, &arr);

    TokCollectCtx ctx = { env, arr, 0 };
    lemma_tokenize(fold_buf, tok_collect_cb, &ctx);

    free(in_buf);
    free(fold_buf);
    return arr;
}

/* lemmaOpen([maskOrSpec]) */
static napi_value LemmaOpen(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);

    lemma_db_t *db = NULL;
    if (argc >= 1) {
        napi_valuetype vtype;
        napi_typeof(env, args[0], &vtype);
        if (vtype == napi_string) {
            size_t slen = 0;
            napi_get_value_string_utf8(env, args[0], NULL, 0, &slen);
            char *spec = (char *)malloc(slen + 1);
            if (spec) {
                napi_get_value_string_utf8(env, args[0], spec, slen + 1, &slen);
                db = (lemma_db_t *)rec_axis_open(spec);
                free(spec);
            }
        } else if (vtype == napi_number) {
            uint32_t mask = 0;
            napi_get_value_uint32(env, args[0], &mask);
            db = lemma_open(mask);
        }
    }
    if (!db) {
        db = lemma_open(0);
    }
    if (!db) {
        napi_throw_error(env, NULL, "Failed to open liblemma index");
        return NULL;
    }

    LemmaDbWrapper *wrap = (LemmaDbWrapper *)calloc(1, sizeof(LemmaDbWrapper));
    if (!wrap) {
        lemma_close(db);
        napi_throw_error(env, NULL, "Out of memory");
        return NULL;
    }
    wrap->db = db;
    wrap->is_open = true;

    napi_value res;
    napi_create_object(env, &res);
    napi_wrap(env, res, wrap, LemmaDbFinalizer, NULL, NULL);
    return res;
}

/* lemmaClose(dbObj) */
static napi_value LemmaClose(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);

    if (argc >= 1) {
        LemmaDbWrapper *wrap = NULL;
        napi_unwrap(env, args[0], (void **)&wrap);
        if (wrap && wrap->is_open && wrap->db) {
            lemma_close(wrap->db);
            wrap->db = NULL;
            wrap->is_open = false;
        }
    }
    napi_value res;
    napi_get_boolean(env, true, &res);
    return res;
}

/* lemmaClear(dbObj) */
static napi_value LemmaClear(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);

    if (argc >= 1) {
        LemmaDbWrapper *wrap = NULL;
        napi_unwrap(env, args[0], (void **)&wrap);
        if (wrap && wrap->is_open && wrap->db) {
            lemma_clear(wrap->db);
        }
    }
    napi_value res;
    napi_get_boolean(env, true, &res);
    return res;
}

/* lemmaIndexRef(dbObj, field, ref, text) */
static napi_value LemmaIndexRef(napi_env env, napi_callback_info info) {
    size_t argc = 4;
    napi_value args[4];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);
    if (argc < 4) {
        napi_throw_type_error(env, NULL, "Expected (dbObj, field, ref, text)");
        return NULL;
    }

    LemmaDbWrapper *wrap = NULL;
    napi_unwrap(env, args[0], (void **)&wrap);
    if (!wrap || !wrap->is_open || !wrap->db) {
        napi_throw_error(env, NULL, "Lemma DB is closed or invalid");
        return NULL;
    }

    char field_buf[128] = LEMMA_AXIS_TEXT_FIELD;
    size_t flen = 0;
    napi_get_value_string_utf8(env, args[1], field_buf, sizeof(field_buf) - 1, &flen);

    uint32_t ref = 0;
    napi_get_value_uint32(env, args[2], &ref);

    size_t tlen = 0;
    napi_get_value_string_utf8(env, args[3], NULL, 0, &tlen);
    char *text = (char *)malloc(tlen + 1);
    if (!text) {
        napi_throw_error(env, NULL, "Out of memory");
        return NULL;
    }
    napi_get_value_string_utf8(env, args[3], text, tlen + 1, &tlen);

    int rc = lemma_index_ref(wrap->db, field_buf, (rec_ref_t)ref, text);
    free(text);

    napi_value res;
    napi_get_boolean(env, rc == 0, &res);
    return res;
}

/* lemmaUnindexRef(dbObj, field, ref) */
static napi_value LemmaUnindexRef(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);
    if (argc < 3) {
        napi_throw_type_error(env, NULL, "Expected (dbObj, field, ref)");
        return NULL;
    }

    LemmaDbWrapper *wrap = NULL;
    napi_unwrap(env, args[0], (void **)&wrap);
    if (!wrap || !wrap->is_open || !wrap->db) {
        napi_throw_error(env, NULL, "Lemma DB is closed or invalid");
        return NULL;
    }

    char field_buf[128] = LEMMA_AXIS_TEXT_FIELD;
    size_t flen = 0;
    napi_get_value_string_utf8(env, args[1], field_buf, sizeof(field_buf) - 1, &flen);

    uint32_t ref = 0;
    napi_get_value_uint32(env, args[2], &ref);

    int rc = lemma_unindex_ref(wrap->db, field_buf, (rec_ref_t)ref);
    napi_value res;
    napi_get_boolean(env, rc == 0, &res);
    return res;
}

/* lemmaStoreTyped(dbObj, ref, uint8Array[, qtype]) */
static napi_value LemmaStoreTyped(napi_env env, napi_callback_info info) {
    size_t argc = 4;
    napi_value args[4];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);
    if (argc < 3) {
        napi_throw_type_error(env, NULL, "Expected (dbObj, ref, uint8Array[, qtype])");
        return NULL;
    }

    LemmaDbWrapper *wrap = NULL;
    napi_unwrap(env, args[0], (void **)&wrap);
    if (!wrap || !wrap->is_open || !wrap->db) {
        napi_throw_error(env, NULL, "Lemma DB is closed or invalid");
        return NULL;
    }

    uint32_t ref = 0;
    napi_get_value_uint32(env, args[1], &ref);

    bool is_ta = false;
    napi_is_typedarray(env, args[2], &is_ta);
    if (!is_ta) {
        napi_throw_type_error(env, NULL, "Expected TypedArray for binary payload");
        return NULL;
    }

    napi_typedarray_type ta_type;
    size_t byte_len = 0;
    void *data = NULL;
    napi_get_typedarray_info(env, args[2], &ta_type, &byte_len, &data, NULL, NULL);

    uint32_t qtype = 0;
    if (argc >= 4) {
        napi_get_value_uint32(env, args[3], &qtype);
    }

    int rc = rec_axis_store_typed(wrap->db, NULL, (rec_ref_t)ref, data, byte_len, qtype);
    napi_value res;
    napi_get_boolean(env, rc == 0, &res);
    return res;
}

static void count_tokens_cb(const char *tok, size_t len, void *user) {
    (void)tok;
    (void)len;
    size_t *cnt = (size_t *)user;
    (*cnt)++;
}

static size_t count_query_tokens(const char *query) {
    if (!query || !*query) return 0;
    size_t len = strlen(query);
    char *fold = (char *)malloc(len * 4 + 32);
    if (!fold) return 1;
    if (lemma_fold(fold, len * 4 + 32, query) < 0) {
        strcpy(fold, query);
    }
    size_t n = 0;
    lemma_tokenize(fold, count_tokens_cb, &n);
    free(fold);
    return n > 0 ? n : 1;
}

/* lemmaFillTokens(dbObj, field, query[, phrase]) -> number[] */
static napi_value LemmaFillTokens(napi_env env, napi_callback_info info) {
    size_t argc = 4;
    napi_value args[4];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);
    if (argc < 3) {
        napi_throw_type_error(env, NULL, "Expected (dbObj, field, query[, phrase])");
        return NULL;
    }

    LemmaDbWrapper *wrap = NULL;
    napi_unwrap(env, args[0], (void **)&wrap);
    if (!wrap || !wrap->is_open || !wrap->db) {
        napi_throw_error(env, NULL, "Lemma DB is closed or invalid");
        return NULL;
    }

    char field_buf[128] = LEMMA_AXIS_TEXT_FIELD;
    size_t flen = 0;
    napi_get_value_string_utf8(env, args[1], field_buf, sizeof(field_buf) - 1, &flen);

    size_t qlen = 0;
    napi_get_value_string_utf8(env, args[2], NULL, 0, &qlen);
    char *query = (char *)malloc(qlen + 1);
    if (!query) {
        napi_throw_error(env, NULL, "Out of memory");
        return NULL;
    }
    napi_get_value_string_utf8(env, args[2], query, qlen + 1, &qlen);

    bool phrase = false;
    if (argc >= 4) {
        napi_get_value_bool(env, args[3], &phrase);
    }

    rec_set_t *cands = rec_set_new();
    rec_axis_fill_tokens(wrap->db, field_buf, query, phrase ? 1 : 0, cands);
    free(query);

    size_t n = rec_set_count(cands);
    const rec_ref_t *refs = rec_set_at(cands);

    napi_value arr;
    napi_create_array_with_length(env, n, &arr);
    for (size_t i = 0; i < n; i++) {
        napi_value v;
        napi_create_uint32(env, (uint32_t)refs[i], &v);
        napi_set_element(env, arr, (uint32_t)i, v);
    }
    rec_set_free(cands);
    return arr;
}

/* lemmaRank(dbObj, field, queryOrMatchedCount, ref) -> number */
static napi_value LemmaRank(napi_env env, napi_callback_info info) {
    size_t argc = 4;
    napi_value args[4];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);
    if (argc < 4) {
        napi_throw_type_error(env, NULL, "Expected (dbObj, field, queryOrMatchedCount, ref)");
        return NULL;
    }

    LemmaDbWrapper *wrap = NULL;
    napi_unwrap(env, args[0], (void **)&wrap);
    if (!wrap || !wrap->is_open || !wrap->db) {
        napi_throw_error(env, NULL, "Lemma DB is closed or invalid");
        return NULL;
    }

    char field_buf[128] = LEMMA_AXIS_TEXT_FIELD;
    size_t flen = 0;
    napi_get_value_string_utf8(env, args[1], field_buf, sizeof(field_buf) - 1, &flen);

    size_t matched = 1;
    napi_valuetype mtype;
    napi_typeof(env, args[2], &mtype);
    if (mtype == napi_number) {
        uint32_t m_u32 = 1;
        napi_get_value_uint32(env, args[2], &m_u32);
        matched = m_u32 > 0 ? (size_t)m_u32 : 1;
    } else if (mtype == napi_string) {
        size_t qlen = 0;
        napi_get_value_string_utf8(env, args[2], NULL, 0, &qlen);
        char *qstr = (char *)malloc(qlen + 1);
        if (qstr) {
            napi_get_value_string_utf8(env, args[2], qstr, qlen + 1, &qlen);
            matched = count_query_tokens(qstr);
            free(qstr);
        }
    }

    uint32_t ref = 0;
    napi_get_value_uint32(env, args[3], &ref);

    struct lemma_rank_ctx rctx = { wrap->db, field_buf, matched };
    float score = 0.0f;
    if (lemma_rank(&rctx, (rec_ref_t)ref, &score) != 0) {
        score = 0.0f;
    }

    napi_value res;
    napi_create_double(env, (double)score, &res);
    return res;
}

/* lemmaSearch(dbObj, field, query[, phrase, topK]) -> Array<{ id, score }> */
static napi_value LemmaSearch(napi_env env, napi_callback_info info) {
    size_t argc = 5;
    napi_value args[5];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);
    if (argc < 3) {
        napi_throw_type_error(env, NULL, "Expected (dbObj, field, query[, phrase, topK])");
        return NULL;
    }

    LemmaDbWrapper *wrap = NULL;
    napi_unwrap(env, args[0], (void **)&wrap);
    if (!wrap || !wrap->is_open || !wrap->db) {
        napi_throw_error(env, NULL, "Lemma DB is closed or invalid");
        return NULL;
    }

    char field_buf[128] = LEMMA_AXIS_TEXT_FIELD;
    size_t flen = 0;
    napi_get_value_string_utf8(env, args[1], field_buf, sizeof(field_buf) - 1, &flen);

    size_t qlen = 0;
    napi_get_value_string_utf8(env, args[2], NULL, 0, &qlen);
    char *query = (char *)malloc(qlen + 1);
    if (!query) {
        napi_throw_error(env, NULL, "Out of memory");
        return NULL;
    }
    napi_get_value_string_utf8(env, args[2], query, qlen + 1, &qlen);

    bool phrase = false;
    if (argc >= 4) {
        napi_get_value_bool(env, args[3], &phrase);
    }

    uint32_t top_k = 100;
    if (argc >= 5) {
        napi_get_value_uint32(env, args[4], &top_k);
        if (top_k == 0) top_k = 100;
    }

    rec_set_t *cands = rec_set_new();
    rec_axis_fill_tokens(wrap->db, field_buf, query, phrase ? 1 : 0, cands);
    size_t matched = count_query_tokens(query);
    free(query);

    size_t n = rec_set_count(cands);
    const rec_ref_t *refs = rec_set_at(cands);

    size_t cap = n < top_k ? n : top_k;
    if (cap == 0) {
        rec_set_free(cands);
        napi_value empty;
        napi_create_array_with_length(env, 0, &empty);
        return empty;
    }

    rec_rank_t *rk = rec_rank_new(cap, -1e9f);
    struct lemma_rank_ctx rctx = { wrap->db, field_buf, matched };
    for (size_t i = 0; i < n; i++) {
        float sc = 0.0f;
        if (lemma_rank(&rctx, refs[i], &sc) == 0) {
            rec_rank_push(rk, refs[i], sc);
        }
    }
    rec_set_free(cands);

    rec_ref_t *out_refs = (rec_ref_t *)malloc(cap * sizeof(rec_ref_t));
    float *out_scores = (float *)malloc(cap * sizeof(float));
    size_t written = rec_rank_sorted(rk, out_refs, out_scores);
    rec_rank_free(rk);

    napi_value arr;
    napi_create_array_with_length(env, written, &arr);
    for (size_t i = 0; i < written; i++) {
        napi_value item, vid, vscore;
        napi_create_object(env, &item);
        napi_create_uint32(env, (uint32_t)out_refs[i], &vid);
        napi_create_double(env, (double)out_scores[i], &vscore);
        napi_set_named_property(env, item, "id", vid);
        napi_set_named_property(env, item, "score", vscore);
        napi_set_element(env, arr, (uint32_t)i, item);
    }

    free(out_refs);
    free(out_scores);
    return arr;
}

/* lemmaTokEncode(text) -> Uint16Array */
static napi_value LemmaTokEncode(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);
    if (argc < 1) {
        napi_throw_type_error(env, NULL, "Expected (text)");
        return NULL;
    }

    size_t slen = 0;
    napi_get_value_string_utf8(env, args[0], NULL, 0, &slen);
    char *src = (char *)malloc(slen + 1);
    if (!src) {
        napi_throw_error(env, NULL, "Out of memory");
        return NULL;
    }
    napi_get_value_string_utf8(env, args[0], src, slen + 1, &slen);

    size_t cap = slen + 16;
    uint16_t *tmp = (uint16_t *)malloc(cap * sizeof(uint16_t));
    if (!tmp) {
        free(src);
        napi_throw_error(env, NULL, "Out of memory");
        return NULL;
    }

    size_t n_toks = 0;
    if (lemma_tok_encode(NULL, src, slen, tmp, cap, &n_toks) != 0) {
        free(src);
        free(tmp);
        napi_throw_error(env, NULL, "lemma_tok_encode failed");
        return NULL;
    }
    free(src);

    void *ab_data = NULL;
    napi_value ab, ta;
    napi_create_arraybuffer(env, n_toks * sizeof(uint16_t), &ab_data, &ab);
    if (n_toks > 0 && ab_data) {
        memcpy(ab_data, tmp, n_toks * sizeof(uint16_t));
    }
    free(tmp);

    napi_create_typedarray(env, napi_uint16_array, n_toks, ab, 0, &ta);
    return ta;
}

/* lemmaTokDecode(uint16Array) -> string */
static napi_value LemmaTokDecode(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);
    if (argc < 1) {
        napi_throw_type_error(env, NULL, "Expected (uint16Array)");
        return NULL;
    }

    bool is_ta = false;
    napi_is_typedarray(env, args[0], &is_ta);
    if (!is_ta) {
        napi_throw_type_error(env, NULL, "Expected Uint16Array");
        return NULL;
    }

    napi_typedarray_type ta_type;
    size_t elem_len = 0;
    void *data = NULL;
    napi_get_typedarray_info(env, args[0], &ta_type, &elem_len, &data, NULL, NULL);
    if (ta_type != napi_uint16_array) {
        napi_throw_type_error(env, NULL, "Expected Uint16Array");
        return NULL;
    }

    size_t cap = elem_len * 32 + 64;
    char *dst = (char *)malloc(cap);
    if (!dst) {
        napi_throw_error(env, NULL, "Out of memory");
        return NULL;
    }

    size_t written = 0;
    if (lemma_tok_decode(NULL, (const uint16_t *)data, elem_len, dst, cap, &written) != 0) {
        free(dst);
        napi_throw_error(env, NULL, "lemma_tok_decode failed");
        return NULL;
    }

    napi_value res;
    napi_create_string_utf8(env, dst, written, &res);
    free(dst);
    return res;
}

static napi_value Init(napi_env env, napi_value exports) {
    napi_property_descriptor desc[] = {
        {"fold", NULL, LemmaFold, NULL, NULL, NULL, napi_default, NULL},
        {"tokenize", NULL, LemmaTokenize, NULL, NULL, NULL, napi_default, NULL},
        {"open", NULL, LemmaOpen, NULL, NULL, NULL, napi_default, NULL},
        {"close", NULL, LemmaClose, NULL, NULL, NULL, napi_default, NULL},
        {"clear", NULL, LemmaClear, NULL, NULL, NULL, napi_default, NULL},
        {"indexRef", NULL, LemmaIndexRef, NULL, NULL, NULL, napi_default, NULL},
        {"unindexRef", NULL, LemmaUnindexRef, NULL, NULL, NULL, napi_default, NULL},
        {"storeTyped", NULL, LemmaStoreTyped, NULL, NULL, NULL, napi_default, NULL},
        {"fillTokens", NULL, LemmaFillTokens, NULL, NULL, NULL, napi_default, NULL},
        {"rank", NULL, LemmaRank, NULL, NULL, NULL, napi_default, NULL},
        {"search", NULL, LemmaSearch, NULL, NULL, NULL, napi_default, NULL},
        {"tokEncode", NULL, LemmaTokEncode, NULL, NULL, NULL, napi_default, NULL},
        {"tokDecode", NULL, LemmaTokDecode, NULL, NULL, NULL, napi_default, NULL}
    };
    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);
    return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)
