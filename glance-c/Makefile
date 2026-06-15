# glance-c — C rewrite of glance.
#   glance         the TUI: Reader + Insert modes (Split lands in a later slice)
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
        $(SRC)/toc.c $(SRC)/util.c
HDRS := $(wildcard $(SRC)/*.h)   # rebuild on any header change

.PHONY: all test clean

all: glance glance-render

glance: $(SRC)/main.c $(SRC)/tui.c $(SRC)/editor.c $(CORE) $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(SRC)/main.c $(SRC)/tui.c $(SRC)/editor.c $(CORE) $(MD4C_LIBS) $(NC_LIBS)

glance-render: $(SRC)/main_render.c $(CORE) $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(SRC)/main_render.c $(CORE) $(MD4C_LIBS)

# Each test binary links the modules it exercises; all run under ASan/UBSan.
TCFLAGS := -std=c11 -g -fsanitize=address,undefined -Wall -Wextra
test:
	@set -e; \
	$(CC) $(TCFLAGS) -o build-t-editor tests/editor_test.c $(SRC)/editor.c $(SRC)/util.c && ./build-t-editor; \
	$(CC) $(TCFLAGS) -o build-t-preprocess tests/preprocess_test.c $(SRC)/preprocess.c && ./build-t-preprocess; \
	$(CC) $(TCFLAGS) $(shell pkg-config --cflags md4c) -o build-t-search tests/search_test.c \
	  $(SRC)/search.c $(SRC)/render.c $(SRC)/preprocess.c $(SRC)/util.c $(shell pkg-config --libs md4c) && ./build-t-search; \
	$(CC) $(TCFLAGS) $(shell pkg-config --cflags md4c) -o build-t-toc tests/toc_test.c \
	  $(SRC)/toc.c $(SRC)/render.c $(SRC)/preprocess.c $(SRC)/util.c $(shell pkg-config --libs md4c) && ./build-t-toc; \
	rm -rf build-t-*

clean:
	rm -f glance glance-render build-t-*
	rm -rf build *.dSYM
