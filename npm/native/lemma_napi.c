#include <node_api.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <ttypt/lemma.h>

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
        /* Fallback to raw copy if fold fails */
        strncpy(out_buf, in_buf, out_cap - 1);
        out_buf[out_cap - 1] = '\0';
    }

    napi_value res;
    napi_create_string_utf8(env, out_buf, NAPI_AUTO_LENGTH, &res);

    free(in_buf);
    free(out_buf);
    return res;
}

static napi_value Init(napi_env env, napi_value exports) {
    napi_property_descriptor desc[] = {
        {"fold", NULL, LemmaFold, NULL, NULL, NULL, napi_default, NULL}
    };
    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);
    return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)
