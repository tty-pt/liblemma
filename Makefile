FOLDER := ttypt
all := liblemma lemma_test lemma_prop_test lemma_axis_store_test lemma_axis_roundtrip_test lemma_axis_rebuild_test lemma_tok_test

LDLIBS-liblemma := -lcorm
liblemma-obj-y := src/token.o src/tok.o
LDLIBS-lemma_test := -llemma -lcorm
LDLIBS-lemma_prop_test := -llemma -lcorm
LDLIBS-lemma_axis_store_test := -llemma -lcorm
LDLIBS-lemma_axis_roundtrip_test := -llemma -lcorm
LDLIBS-lemma_axis_rebuild_test := -llemma -lcorm -lstele
LDLIBS-lemma_tok_test := -llemma -lcorm

WASM_SRCS := src/token.c npm/wasm/lemma_wasm.c
WASM_EXPORTS := lemma_fold lemma_list_contains lemma_list_append lemma_list_normalize

include ../mk/include.mk

test: all
	./bin/lemma_test
	./bin/lemma_prop_test
	./bin/lemma_axis_store_test
	./bin/lemma_axis_roundtrip_test
	./bin/lemma_tok_test
	rm -rf /tmp/lemma-rebuild-test-*
	./bin/lemma_axis_rebuild_test
	rm -rf /tmp/lemma-rebuild-test-*

# Optional Node.js & WebAssembly bindings
NPM ?= npm

npm-build:
	@if command -v $(NPM) >/dev/null 2>&1; then \
		(cd npm && $(NPM) run build); \
	else \
		echo "Skipping npm build — $(NPM) not found"; \
	fi

npm-test: npm-build
	@if command -v $(NPM) >/dev/null 2>&1; then \
		(cd npm && $(NPM) test); \
	fi

