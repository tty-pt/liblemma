#include "ttypt/lemma.h"
#include <ttypt/iso.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *out;
    size_t out_cap;
    size_t written;
    size_t count;
} tok_ctx_t;

static void tok_collect_cb(const char *tok, size_t len, void *user) {
    tok_ctx_t *ctx = (tok_ctx_t *)user;
    if (ctx->written + len + 1 < ctx->out_cap) {
        memcpy(ctx->out + ctx->written, tok, len);
        ctx->written += len;
        ctx->out[ctx->written++] = '\n';
        ctx->count++;
    }
}

ISO_EXPORT(uint32_t, lemma_wasm_tokenize, const char *text, char *out, size_t out_cap) {
    if (!text || !out || out_cap == 0) return 0;
    out[0] = '\0';
    tok_ctx_t ctx = { out, out_cap, 0, 0 };
    lemma_tokenize(text, tok_collect_cb, &ctx);
    if (ctx.written < out_cap) out[ctx.written] = '\0';
    return (uint32_t)ctx.count;
}
