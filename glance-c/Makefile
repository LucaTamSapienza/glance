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

glance: $(SRC)/main.c $(SRC)/tui.c $(SRC)/tui.h $(RENDER_SRC) $(RENDER_HDR)
	$(CC) $(CFLAGS) -o $@ $(SRC)/main.c $(SRC)/tui.c $(RENDER_SRC) $(MD4C_LIBS) $(NC_LIBS)

glance-render: $(SRC)/main_render.c $(RENDER_SRC) $(RENDER_HDR)
	$(CC) $(CFLAGS) -o $@ $(SRC)/main_render.c $(RENDER_SRC) $(MD4C_LIBS)

clean:
	rm -f glance glance-render
	rm -rf build *.dSYM
