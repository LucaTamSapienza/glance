# glance-c — C rewrite of glance.
#   glance         the TUI (Reader mode; Insert/Split land in later slices)
#   glance-render  render-only CLI: Markdown -> ANSI on stdout (file or stdin)
CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter
CFLAGS  += $(shell pkg-config --cflags md4c notcurses)

MD4C_LIBS := $(shell pkg-config --libs md4c)
NC_LIBS   := $(shell pkg-config --libs notcurses)

SRC := src

.PHONY: all clean

all: glance glance-render

# shared renderer objects
RENDER_SRC := $(SRC)/render.c $(SRC)/doc_ansi.c
RENDER_HDR := $(SRC)/render.h

glance: $(SRC)/main.c $(SRC)/tui.c $(SRC)/tui.h $(SRC)/editor.c $(SRC)/editor.h $(RENDER_SRC) $(RENDER_HDR)
	$(CC) $(CFLAGS) -o $@ $(SRC)/main.c $(SRC)/tui.c $(SRC)/editor.c $(RENDER_SRC) $(MD4C_LIBS) $(NC_LIBS)

glance-render: $(SRC)/main_render.c $(RENDER_SRC) $(RENDER_HDR)
	$(CC) $(CFLAGS) -o $@ $(SRC)/main_render.c $(RENDER_SRC) $(MD4C_LIBS)

.PHONY: test
# unit tests (pure modules) under ASan/UBSan
test:
	$(CC) -std=c11 -g -fsanitize=address,undefined -Wall -Wextra \
	  -o build-editor-test tests/editor_test.c $(SRC)/editor.c
	./build-editor-test
	@rm -f build-editor-test

clean:
	rm -f glance glance-render build-editor-test
	rm -rf build *.dSYM
