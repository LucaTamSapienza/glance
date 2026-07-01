# glance — a terminal Markdown reader/editor in C.
#   glance         the TUI: Reader + Insert + Split modes, vault navigation
#   glance-render  render-only CLI: Markdown -> ANSI on stdout (file or stdin)
#   make test      unit tests for the pure modules, under UBSan (+ ASan where its
#                  runtime can initialize; see the probe in the `test` recipe)
CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter
CFLAGS  += $(shell pkg-config --cflags md4c notcurses)

MD4C_LIBS := $(shell pkg-config --libs md4c)
NC_LIBS   := $(shell pkg-config --libs notcurses)

SRC := src

# Where `make install` puts the two binaries (override: make install PREFIX=~/.local).
PREFIX ?= /usr/local
BINDIR := $(DESTDIR)$(PREFIX)/bin

# renderer + shared helpers, linked into both binaries
CORE := $(SRC)/render.c $(SRC)/doc_ansi.c $(SRC)/doc_html.c $(SRC)/preprocess.c $(SRC)/theme.c \
        $(SRC)/search.c $(SRC)/toc.c $(SRC)/fs_save.c $(SRC)/vault.c $(SRC)/graph.c \
        $(SRC)/highlight.c $(SRC)/image_size.c $(SRC)/util.c
HDRS := $(wildcard $(SRC)/*.h)   # rebuild on any header change

.PHONY: all test clean install uninstall

all: glance glance-render

GUI := $(SRC)/main.c $(SRC)/tui.c $(SRC)/editor.c $(SRC)/fswatch.c \
       $(SRC)/clipboard.c $(SRC)/completion.c $(SRC)/agent.c $(SRC)/legend.c \
       $(SRC)/progress.c $(SRC)/section.c $(SRC)/receipt.c $(SRC)/bm25.c \
       $(SRC)/context.c $(SRC)/embed.c $(SRC)/edit.c $(SRC)/json.c $(SRC)/mcp.c \
       $(SRC)/export.c $(SRC)/fuzzy.c
glance: $(GUI) $(CORE) $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(GUI) $(CORE) $(MD4C_LIBS) $(NC_LIBS) -lm

glance-render: $(SRC)/main_render.c $(CORE) $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(SRC)/main_render.c $(CORE) $(MD4C_LIBS)

# Each test binary links the modules it exercises. The sanitizer set is chosen at
# run time by the probe at the top of the `test` recipe below — AddressSanitizer +
# UBSan where asan can start, UBSan alone where it can't. asan's runtime deadlocks
# during init on macOS 26: its shadow-memory setup enumerates the dyld shared
# cache, which now allocates, re-entering asan's own malloc interceptor and
# spinning forever on the init lock. A hard-coded -fsanitize=address would make
# `make test` hang (at 100% CPU) on the first suite instead of running, so it is
# added only after the probe confirms a trivial asan binary can actually run.
TCFLAGS := -std=c11 -g -Wall -Wextra
test:
	@set -e; \
	san=address,undefined; \
	echo 'int main(void){return 0;}' > .san-probe.c; \
	if $(CC) -fsanitize=address -o .san-probe .san-probe.c 2>/dev/null; then \
		./.san-probe & pp=$$!; \
		( sleep 5; kill -9 $$pp 2>/dev/null ) & wd=$$!; \
		if wait $$pp 2>/dev/null; then :; \
		else san=undefined; echo "make test: AddressSanitizer can't initialize here (known macOS 26 asan deadlock) -- using UBSan only"; fi; \
		kill -9 $$wd 2>/dev/null || true; wait $$wd 2>/dev/null || true; \
	else san=undefined; echo "make test: AddressSanitizer unavailable -- using UBSan only"; fi; \
	rm -f .san-probe .san-probe.c; \
	CX="$(CC) $(TCFLAGS) -fsanitize=$$san"; \
	echo "make test: building suites under -fsanitize=$$san"; \
	$$CX -o build-t-editor tests/editor_test.c $(SRC)/editor.c $(SRC)/util.c && ./build-t-editor; \
	$$CX -o build-t-preprocess tests/preprocess_test.c $(SRC)/preprocess.c && ./build-t-preprocess; \
	$$CX $(shell pkg-config --cflags md4c) -o build-t-search tests/search_test.c \
	  $(SRC)/search.c $(SRC)/render.c $(SRC)/theme.c $(SRC)/preprocess.c $(SRC)/highlight.c $(SRC)/image_size.c $(SRC)/util.c $(shell pkg-config --libs md4c) && ./build-t-search; \
	$$CX $(shell pkg-config --cflags md4c) -o build-t-toc tests/toc_test.c \
	  $(SRC)/toc.c $(SRC)/render.c $(SRC)/theme.c $(SRC)/preprocess.c $(SRC)/highlight.c $(SRC)/image_size.c $(SRC)/util.c $(shell pkg-config --libs md4c) && ./build-t-toc; \
	$$CX -o build-t-fssave tests/fs_save_test.c $(SRC)/fs_save.c && ./build-t-fssave; \
	$$CX -o build-t-fswatch tests/fswatch_test.c $(SRC)/fswatch.c && ./build-t-fswatch; \
	$$CX -o build-t-completion tests/completion_test.c $(SRC)/completion.c && ./build-t-completion; \
	$$CX -o build-t-fuzzy tests/fuzzy_test.c $(SRC)/fuzzy.c && ./build-t-fuzzy; \
	$$CX -o build-t-legend tests/legend_test.c $(SRC)/legend.c && ./build-t-legend; \
	$$CX -o build-t-progress tests/progress_test.c $(SRC)/progress.c && ./build-t-progress; \
	$$CX -o build-t-receipt tests/receipt_test.c $(SRC)/receipt.c && ./build-t-receipt; \
	$$CX -lm -o build-t-bm25 tests/bm25_test.c $(SRC)/bm25.c && ./build-t-bm25; \
	$$CX -o build-t-context tests/context_test.c $(SRC)/context.c && ./build-t-context; \
	$$CX -lm -o build-t-embed tests/embed_test.c $(SRC)/embed.c && ./build-t-embed; \
	$$CX $(shell pkg-config --cflags md4c) -o build-t-edit tests/edit_test.c \
	  $(SRC)/edit.c $(SRC)/section.c $(SRC)/render.c $(SRC)/theme.c $(SRC)/preprocess.c $(SRC)/toc.c $(SRC)/highlight.c $(SRC)/image_size.c $(SRC)/util.c $(shell pkg-config --libs md4c) && ./build-t-edit; \
	$$CX -lm -o build-t-json tests/json_test.c $(SRC)/json.c $(SRC)/util.c && ./build-t-json; \
	$$CX $(shell pkg-config --cflags md4c) -o build-t-mcp tests/mcp_test.c \
	  $(SRC)/mcp.c $(SRC)/json.c $(SRC)/agent.c $(SRC)/section.c $(SRC)/receipt.c $(SRC)/context.c $(SRC)/bm25.c $(SRC)/embed.c $(SRC)/edit.c $(SRC)/fs_save.c $(SRC)/render.c $(SRC)/theme.c $(SRC)/preprocess.c $(SRC)/toc.c $(SRC)/vault.c $(SRC)/graph.c $(SRC)/highlight.c $(SRC)/image_size.c $(SRC)/util.c \
	  $(shell pkg-config --libs md4c) -lm && ./build-t-mcp; \
	$$CX $(shell pkg-config --cflags md4c) -o build-t-section tests/section_test.c \
	  $(SRC)/section.c $(SRC)/render.c $(SRC)/theme.c $(SRC)/preprocess.c $(SRC)/toc.c $(SRC)/highlight.c $(SRC)/image_size.c $(SRC)/util.c $(shell pkg-config --libs md4c) && ./build-t-section; \
	$$CX $(shell pkg-config --cflags md4c) -o build-t-theme tests/theme_test.c $(SRC)/theme.c && ./build-t-theme; \
	$$CX -o build-t-highlight tests/highlight_test.c $(SRC)/highlight.c && ./build-t-highlight; \
	$$CX -o build-t-imagesize tests/image_size_test.c $(SRC)/image_size.c && ./build-t-imagesize; \
	$$CX $(shell pkg-config --cflags md4c) -o build-t-render tests/render_test.c \
	  $(SRC)/render.c $(SRC)/doc_ansi.c $(SRC)/preprocess.c $(SRC)/theme.c $(SRC)/highlight.c $(SRC)/image_size.c $(SRC)/util.c $(shell pkg-config --libs md4c) && ./build-t-render; \
	$$CX $(shell pkg-config --cflags md4c) -o build-t-dochtml tests/doc_html_test.c \
	  $(SRC)/doc_html.c $(SRC)/theme.c $(SRC)/highlight.c $(shell pkg-config --libs md4c) && ./build-t-dochtml; \
	$$CX $(shell pkg-config --cflags md4c) -o build-t-export tests/export_test.c \
	  $(SRC)/export.c $(SRC)/doc_html.c $(SRC)/theme.c $(SRC)/highlight.c $(SRC)/fs_save.c $(SRC)/util.c $(shell pkg-config --libs md4c) && ./build-t-export; \
	$$CX $(shell pkg-config --cflags md4c) -o build-t-vault tests/vault_test.c \
	  $(SRC)/vault.c $(shell pkg-config --libs md4c) && ./build-t-vault; \
	$$CX $(shell pkg-config --cflags md4c) -o build-t-agent tests/agent_test.c \
	  $(SRC)/agent.c $(SRC)/section.c $(SRC)/receipt.c $(SRC)/context.c $(SRC)/bm25.c $(SRC)/embed.c $(SRC)/edit.c $(SRC)/fs_save.c $(SRC)/render.c $(SRC)/theme.c $(SRC)/preprocess.c $(SRC)/toc.c $(SRC)/vault.c $(SRC)/graph.c $(SRC)/highlight.c $(SRC)/image_size.c $(SRC)/util.c \
	  $(shell pkg-config --libs md4c) -lm && ./build-t-agent; \
	$$CX $(shell pkg-config --cflags md4c) -o build-t-graph tests/graph_test.c \
	  $(SRC)/graph.c $(SRC)/vault.c $(SRC)/util.c $(shell pkg-config --libs md4c) && ./build-t-graph; \
	rm -rf build-t-*

# Install both binaries onto PATH (default /usr/local/bin; may need sudo).
install: all
	install -d $(BINDIR)
	install -m 755 glance $(BINDIR)/glance
	install -m 755 glance-render $(BINDIR)/glance-render
	@echo "installed glance and glance-render to $(BINDIR)"

uninstall:
	rm -f $(BINDIR)/glance $(BINDIR)/glance-render

clean:
	rm -f glance glance-render build-t-*
	rm -rf build *.dSYM
