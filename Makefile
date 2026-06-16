# glance — a terminal Markdown reader/editor in C.
#   glance         the TUI: Reader + Insert + Split modes, vault navigation
#   glance-render  render-only CLI: Markdown -> ANSI on stdout (file or stdin)
#   make test      unit tests for the pure modules, under ASan/UBSan
CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter
CFLAGS  += $(shell pkg-config --cflags md4c notcurses)

MD4C_LIBS := $(shell pkg-config --libs md4c)
NC_LIBS   := $(shell pkg-config --libs notcurses)

SRC := src

# renderer + shared helpers, linked into both binaries
CORE := $(SRC)/render.c $(SRC)/doc_ansi.c $(SRC)/preprocess.c $(SRC)/search.c \
        $(SRC)/toc.c $(SRC)/fs_save.c $(SRC)/vault.c $(SRC)/graph.c \
        $(SRC)/highlight.c $(SRC)/util.c
HDRS := $(wildcard $(SRC)/*.h)   # rebuild on any header change

.PHONY: all test clean

all: glance glance-render

GUI := $(SRC)/main.c $(SRC)/tui.c $(SRC)/editor.c $(SRC)/fswatch.c \
       $(SRC)/clipboard.c $(SRC)/completion.c $(SRC)/agent.c
glance: $(GUI) $(CORE) $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(GUI) $(CORE) $(MD4C_LIBS) $(NC_LIBS)

glance-render: $(SRC)/main_render.c $(CORE) $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(SRC)/main_render.c $(CORE) $(MD4C_LIBS)

# Each test binary links the modules it exercises; all run under ASan/UBSan.
TCFLAGS := -std=c11 -g -fsanitize=address,undefined -Wall -Wextra
test:
	@set -e; \
	$(CC) $(TCFLAGS) -o build-t-editor tests/editor_test.c $(SRC)/editor.c $(SRC)/util.c && ./build-t-editor; \
	$(CC) $(TCFLAGS) -o build-t-preprocess tests/preprocess_test.c $(SRC)/preprocess.c && ./build-t-preprocess; \
	$(CC) $(TCFLAGS) $(shell pkg-config --cflags md4c) -o build-t-search tests/search_test.c \
	  $(SRC)/search.c $(SRC)/render.c $(SRC)/preprocess.c $(SRC)/highlight.c $(SRC)/util.c $(shell pkg-config --libs md4c) && ./build-t-search; \
	$(CC) $(TCFLAGS) $(shell pkg-config --cflags md4c) -o build-t-toc tests/toc_test.c \
	  $(SRC)/toc.c $(SRC)/render.c $(SRC)/preprocess.c $(SRC)/highlight.c $(SRC)/util.c $(shell pkg-config --libs md4c) && ./build-t-toc; \
	$(CC) $(TCFLAGS) -o build-t-fssave tests/fs_save_test.c $(SRC)/fs_save.c && ./build-t-fssave; \
	$(CC) $(TCFLAGS) -o build-t-completion tests/completion_test.c $(SRC)/completion.c && ./build-t-completion; \
	$(CC) $(TCFLAGS) -o build-t-highlight tests/highlight_test.c $(SRC)/highlight.c && ./build-t-highlight; \
	$(CC) $(TCFLAGS) $(shell pkg-config --cflags md4c) -o build-t-vault tests/vault_test.c \
	  $(SRC)/vault.c $(shell pkg-config --libs md4c) && ./build-t-vault; \
	$(CC) $(TCFLAGS) $(shell pkg-config --cflags md4c) -o build-t-agent tests/agent_test.c \
	  $(SRC)/agent.c $(SRC)/render.c $(SRC)/preprocess.c $(SRC)/toc.c $(SRC)/vault.c $(SRC)/graph.c $(SRC)/highlight.c $(SRC)/util.c \
	  $(shell pkg-config --libs md4c) && ./build-t-agent; \
	$(CC) $(TCFLAGS) $(shell pkg-config --cflags md4c) -o build-t-graph tests/graph_test.c \
	  $(SRC)/graph.c $(SRC)/vault.c $(SRC)/util.c $(shell pkg-config --libs md4c) && ./build-t-graph; \
	rm -rf build-t-*

clean:
	rm -f glance glance-render build-t-*
	rm -rf build *.dSYM
