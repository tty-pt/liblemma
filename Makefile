FOLDER := ttypt
all := liblemma lemma_test lemma_prop_test lemma_axis_store_test lemma_axis_roundtrip_test lemma_axis_rebuild_test

LDLIBS-liblemma := -lcorm
liblemma-obj-y := src/token.o
LDLIBS-lemma_test := -llemma -lcorm
LDLIBS-lemma_prop_test := -llemma -lcorm
LDLIBS-lemma_axis_store_test := -llemma -lcorm
LDLIBS-lemma_axis_roundtrip_test := -llemma -lcorm
LDLIBS-lemma_axis_rebuild_test := -llemma -lcorm -lqsys

CFLAGS += -I/home/quirinpa/site/external/libcorm/include

include ../mk/include.mk

test: all
	LD_LIBRARY_PATH=$(abspath lib) ./bin/lemma_test
	LD_LIBRARY_PATH=$(abspath lib) ./bin/lemma_prop_test
	LD_LIBRARY_PATH=$(abspath lib) ./bin/lemma_axis_store_test
	LD_LIBRARY_PATH=$(abspath lib) ./bin/lemma_axis_roundtrip_test
	rm -rf /tmp/lemma-rebuild-test-*
	LD_LIBRARY_PATH=$(abspath lib) ./bin/lemma_axis_rebuild_test
	rm -rf /tmp/lemma-rebuild-test-*
