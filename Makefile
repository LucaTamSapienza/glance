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

# Where `make install` puts the two binaries (override: make install PREFIX=~/.local).
PREFIX ?= /usr/local
BINDIR := $(DESTDIR)$(PREFIX)/bin

# renderer + shared helpers, linked into both binaries
CORE := $(SRC)/render.c $(SRC)/doc_ansi.c $(SRC)/doc_html.c $(SRC)/preprocess.c $(SRC)/theme.c \
        $(SRC)/search.c $(SRC)/toc.c $(SRC)/fs_save.c $(SRC)/vault.c $(SRC)/graph.c \
        $(SRC)/highlight.c $(SRC)/image_size.c $(SRC)/util.c
HDRS := $(wildcard $(SRC)/*.h)   # rebuild on any header change

.PHONY: all test clean install uninstall semantic-smoke

all: glance glance-render

GUI := $(SRC)/main.c $(SRC)/tui.c $(SRC)/editor.c $(SRC)/fswatch.c \
       $(SRC)/clipboard.c $(SRC)/completion.c $(SRC)/agent.c $(SRC)/legend.c \
       $(SRC)/progress.c $(SRC)/section.c $(SRC)/receipt.c $(SRC)/bm25.c \
       $(SRC)/context.c $(SRC)/embed.c $(SRC)/edit.c $(SRC)/json.c $(SRC)/mcp.c \
       $(SRC)/export.c $(SRC)/fuzzy.c

# --- Optional semantic embeddings (All-MiniLM-L6-v2 via vendored llama.cpp) ----
# OFF by default: the standard build carries no llama dependency and uses the
# dependency-free hashing embedder. Enable with:  make GLANCE_SEMANTIC=1
# (builds the vendored static libs once, then links them into glance).
LLAMA_DIR   := third_party/llama.cpp
LLAMA_BUILD := $(LLAMA_DIR)/build-static
LLAMA_A     := $(LLAMA_BUILD)/src/libllama.a \
               $(LLAMA_BUILD)/ggml/src/libggml.a \
               $(LLAMA_BUILD)/ggml/src/libggml-cpu.a \
               $(LLAMA_BUILD)/ggml/src/ggml-metal/libggml-metal.a \
               $(LLAMA_BUILD)/ggml/src/ggml-blas/libggml-blas.a \
               $(LLAMA_BUILD)/ggml/src/libggml-base.a
LLAMA_INC   := -I$(LLAMA_DIR)/include -I$(LLAMA_DIR)/ggml/include
LLAMA_FW    := -lc++ -framework Foundation -framework Metal -framework MetalKit \
               -framework Accelerate -framework QuartzCore

SEM_DEPS :=
SEM_LINK :=
ifeq ($(GLANCE_SEMANTIC),1)
CFLAGS   += -DGLANCE_SEMANTIC $(LLAMA_INC)
GUI      += $(SRC)/embed_minilm.c
SEM_DEPS := $(LLAMA_BUILD)/src/libllama.a
SEM_LINK := $(LLAMA_A) $(LLAMA_FW)
endif

# Build the vendored llama.cpp as static libs (only the `llama` target: skips the
# app/httplib/openssl target that needs a generated build-info.h).
$(LLAMA_BUILD)/src/libllama.a:
	cmake -B $(LLAMA_BUILD) -S $(LLAMA_DIR) -DCMAKE_BUILD_TYPE=Release \
	  -DBUILD_SHARED_LIBS=OFF -DLLAMA_CURL=OFF \
	  -DLLAMA_BUILD_TESTS=OFF -DLLAMA_BUILD_EXAMPLES=OFF \
	  -DLLAMA_BUILD_TOOLS=OFF -DLLAMA_BUILD_SERVER=OFF \
	  -DGGML_METAL=ON -DGGML_METAL_EMBED_LIBRARY=ON
	cmake --build $(LLAMA_BUILD) --target llama -j
# ------------------------------------------------------------------------------

glance: $(SEM_DEPS) $(GUI) $(CORE) $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(GUI) $(CORE) $(MD4C_LIBS) $(NC_LIBS) -lm $(SEM_LINK)

glance-render: $(SRC)/main_render.c $(CORE) $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(SRC)/main_render.c $(CORE) $(MD4C_LIBS)

# Each test binary links the modules it exercises; all run under ASan/UBSan.
TCFLAGS := -std=c11 -g -fsanitize=address,undefined -Wall -Wextra
test:
	@set -e; \
	$(CC) $(TCFLAGS) -o build-t-editor tests/editor_test.c $(SRC)/editor.c $(SRC)/util.c && ./build-t-editor; \
	$(CC) $(TCFLAGS) -o build-t-preprocess tests/preprocess_test.c $(SRC)/preprocess.c && ./build-t-preprocess; \
	$(CC) $(TCFLAGS) $(shell pkg-config --cflags md4c) -o build-t-search tests/search_test.c \
	  $(SRC)/search.c $(SRC)/render.c $(SRC)/theme.c $(SRC)/preprocess.c $(SRC)/highlight.c $(SRC)/image_size.c $(SRC)/util.c $(shell pkg-config --libs md4c) && ./build-t-search; \
	$(CC) $(TCFLAGS) $(shell pkg-config --cflags md4c) -o build-t-toc tests/toc_test.c \
	  $(SRC)/toc.c $(SRC)/render.c $(SRC)/theme.c $(SRC)/preprocess.c $(SRC)/highlight.c $(SRC)/image_size.c $(SRC)/util.c $(shell pkg-config --libs md4c) && ./build-t-toc; \
	$(CC) $(TCFLAGS) -o build-t-fssave tests/fs_save_test.c $(SRC)/fs_save.c && ./build-t-fssave; \
	$(CC) $(TCFLAGS) -o build-t-fswatch tests/fswatch_test.c $(SRC)/fswatch.c && ./build-t-fswatch; \
	$(CC) $(TCFLAGS) -o build-t-completion tests/completion_test.c $(SRC)/completion.c && ./build-t-completion; \
	$(CC) $(TCFLAGS) -o build-t-fuzzy tests/fuzzy_test.c $(SRC)/fuzzy.c && ./build-t-fuzzy; \
	$(CC) $(TCFLAGS) -o build-t-legend tests/legend_test.c $(SRC)/legend.c && ./build-t-legend; \
	$(CC) $(TCFLAGS) -o build-t-progress tests/progress_test.c $(SRC)/progress.c && ./build-t-progress; \
	$(CC) $(TCFLAGS) -o build-t-receipt tests/receipt_test.c $(SRC)/receipt.c && ./build-t-receipt; \
	$(CC) $(TCFLAGS) -lm -o build-t-bm25 tests/bm25_test.c $(SRC)/bm25.c && ./build-t-bm25; \
	$(CC) $(TCFLAGS) -o build-t-context tests/context_test.c $(SRC)/context.c && ./build-t-context; \
	$(CC) $(TCFLAGS) -lm -o build-t-embed tests/embed_test.c $(SRC)/embed.c && ./build-t-embed; \
	$(CC) $(TCFLAGS) $(shell pkg-config --cflags md4c) -o build-t-edit tests/edit_test.c \
	  $(SRC)/edit.c $(SRC)/section.c $(SRC)/render.c $(SRC)/theme.c $(SRC)/preprocess.c $(SRC)/toc.c $(SRC)/highlight.c $(SRC)/image_size.c $(SRC)/util.c $(shell pkg-config --libs md4c) && ./build-t-edit; \
	$(CC) $(TCFLAGS) -lm -o build-t-json tests/json_test.c $(SRC)/json.c $(SRC)/util.c && ./build-t-json; \
	$(CC) $(TCFLAGS) $(shell pkg-config --cflags md4c) -o build-t-mcp tests/mcp_test.c \
	  $(SRC)/mcp.c $(SRC)/json.c $(SRC)/agent.c $(SRC)/section.c $(SRC)/receipt.c $(SRC)/context.c $(SRC)/bm25.c $(SRC)/embed.c $(SRC)/edit.c $(SRC)/fs_save.c $(SRC)/render.c $(SRC)/theme.c $(SRC)/preprocess.c $(SRC)/toc.c $(SRC)/vault.c $(SRC)/graph.c $(SRC)/highlight.c $(SRC)/image_size.c $(SRC)/util.c \
	  $(shell pkg-config --libs md4c) -lm && ./build-t-mcp; \
	$(CC) $(TCFLAGS) $(shell pkg-config --cflags md4c) -o build-t-section tests/section_test.c \
	  $(SRC)/section.c $(SRC)/render.c $(SRC)/theme.c $(SRC)/preprocess.c $(SRC)/toc.c $(SRC)/highlight.c $(SRC)/image_size.c $(SRC)/util.c $(shell pkg-config --libs md4c) && ./build-t-section; \
	$(CC) $(TCFLAGS) $(shell pkg-config --cflags md4c) -o build-t-theme tests/theme_test.c $(SRC)/theme.c && ./build-t-theme; \
	$(CC) $(TCFLAGS) -o build-t-highlight tests/highlight_test.c $(SRC)/highlight.c && ./build-t-highlight; \
	$(CC) $(TCFLAGS) -o build-t-imagesize tests/image_size_test.c $(SRC)/image_size.c && ./build-t-imagesize; \
	$(CC) $(TCFLAGS) $(shell pkg-config --cflags md4c) -o build-t-render tests/render_test.c \
	  $(SRC)/render.c $(SRC)/doc_ansi.c $(SRC)/preprocess.c $(SRC)/theme.c $(SRC)/highlight.c $(SRC)/image_size.c $(SRC)/util.c $(shell pkg-config --libs md4c) && ./build-t-render; \
	$(CC) $(TCFLAGS) $(shell pkg-config --cflags md4c) -o build-t-dochtml tests/doc_html_test.c \
	  $(SRC)/doc_html.c $(SRC)/theme.c $(SRC)/highlight.c $(shell pkg-config --libs md4c) && ./build-t-dochtml; \
	$(CC) $(TCFLAGS) $(shell pkg-config --cflags md4c) -o build-t-export tests/export_test.c \
	  $(SRC)/export.c $(SRC)/doc_html.c $(SRC)/theme.c $(SRC)/highlight.c $(SRC)/fs_save.c $(SRC)/util.c $(shell pkg-config --libs md4c) && ./build-t-export; \
	$(CC) $(TCFLAGS) $(shell pkg-config --cflags md4c) -o build-t-vault tests/vault_test.c \
	  $(SRC)/vault.c $(shell pkg-config --libs md4c) && ./build-t-vault; \
	$(CC) $(TCFLAGS) $(shell pkg-config --cflags md4c) -o build-t-agent tests/agent_test.c \
	  $(SRC)/agent.c $(SRC)/section.c $(SRC)/receipt.c $(SRC)/context.c $(SRC)/bm25.c $(SRC)/embed.c $(SRC)/edit.c $(SRC)/fs_save.c $(SRC)/render.c $(SRC)/theme.c $(SRC)/preprocess.c $(SRC)/toc.c $(SRC)/vault.c $(SRC)/graph.c $(SRC)/highlight.c $(SRC)/image_size.c $(SRC)/util.c \
	  $(shell pkg-config --libs md4c) -lm && ./build-t-agent; \
	$(CC) $(TCFLAGS) $(shell pkg-config --cflags md4c) -o build-t-graph tests/graph_test.c \
	  $(SRC)/graph.c $(SRC)/vault.c $(SRC)/util.c $(shell pkg-config --libs md4c) && ./build-t-graph; \
	rm -rf build-t-*

# Semantic embedder sanity check (needs the vendored llama build + a gguf model).
# Not part of `make test` (which must stay model-less and dependency-light).
#   make semantic-smoke MODEL=/path/to/all-MiniLM-L6-v2.gguf
semantic-smoke: $(LLAMA_BUILD)/src/libllama.a
	$(CC) -std=c11 -O2 -DGLANCE_SEMANTIC $(LLAMA_INC) -o build-t-minilm \
	  tests/embed_minilm_smoke.c $(SRC)/embed_minilm.c $(SRC)/embed.c \
	  $(LLAMA_A) $(LLAMA_FW) -lm
	./build-t-minilm $(MODEL)
	rm -f build-t-minilm

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
