# glance-c — C rewrite of glance

A from-scratch C port of glance, so we own the rendering instead of treating
glamour/glow as a black box. Built as working vertical slices.

## Why C / why own the renderer

Much of the Go code exists to fight glamour's opacity: `preprocess.go` inserts
`GLANCEBLANK` marker paragraphs and strips them back out; `search.go` strips
ANSI to find matches then re-injects SGR; cursor sync *guesses* a proportional
mapping. Owning the renderer (md4c → our callbacks) makes all of that exact and
unnecessary — we decide every byte, and can emit a `rendered_line → source_line`
map for free.

## Stack

| Concern        | Choice                                    | Replaces                         |
|----------------|-------------------------------------------|----------------------------------|
| MD parse       | md4c (GFM ext) — installed                 | goldmark                         |
| Render → cells | our own renderer (`render_md.c`)           | glamour / chroma                 |
| TUI runtime    | notcurses (planes, grapheme cells, input)  | bubbletea/lipgloss/runewidth     |
| Editor widget  | our own line-array/gap-buffer              | bubbles/textarea                 |
| File watch     | kqueue on parent dir                       | fsnotify                         |
| Clipboard      | pbcopy/pbpaste via popen                    | atotto/clipboard                 |
| Syntax hl      | deferred (styled bg now; bat/tree-sitter)  | chroma                           |

## Module map (Go → C)

```
src/render.h/.c   THE renderer: md4c → structured Doc        [DONE — slice 1/2]
src/doc_ansi.c    Doc → ANSI string (CLI + tests sink)        [DONE]
src/tui.h/.c      notcurses front-end: Reader + Insert modes   [DONE — slice 2/3]
src/editor.h/.c   line-array editor model (rune-aware cursor)   [DONE — slice 3]
src/main.c        glance TUI entry: load file/stdin, run tui   [DONE]
src/main_render.c glance-render CLI entry (render-only)        [DONE]
tests/editor_test.c  unit tests for the editor (make test)     [DONE]
-- planned --
src/preprocess.c  tolerant markdown (bold tighten, setext neutralize)
src/toc.c  search.c  completion.c
src/fswatch.c (kqueue)  save.c (atomic write)  clipboard.c  util.c
```

The renderer emits a **structured Doc** (lines of styled runs), not an ANSI
string. Two sinks consume it: `doc_ansi.c` (ANSI, for the CLI/tests) and
`tui.c` (notcurses cells). Owning the cell output is why search/cursor work
later needs no ANSI strip/re-inject.

## Slices

1. **Renderer (DONE)** — md4c → Doc. Headings, bold/italic/code/strike/
   underline spans, links, ordered/unordered/nested lists, blockquotes, fenced
   code blocks, tables, thematic breaks, word-wrap. ASan/UBSan-clean.
2. **Reader TUI (DONE)** — notcurses alt-screen, scrollable view, status bar,
   resize re-render. **Block cursor** (white rectangle) moved with hjkl/arrows,
   Ctrl-D/U half-page, PgUp/PgDn + Ctrl-F/B page, g/Home top, G/End bottom; the
   view scrolls to keep it visible. **vi-style quit: `:q`** (also `:quit`/`:q!`);
   Ctrl-C is an escape hatch. The Reader cursor and the editor cursor sync
   across mode switches *proportionally* (see source-map note — md4c gives no
   offsets, so it's not yet an exact 1:1 line map).
3. **Editor + Insert mode (DONE)** — line-array buffer, rune-aware cursor,
   horizontal + vertical scroll, hardware cursor. **vi-style: `i` enters
   Insert**, Esc returns to Reader and re-renders the edited text. Editing:
   printable insert, Enter split, Backspace/Del (with line join),
   arrows/Home/End, Tab=4 spaces. Source round-trips through the buffer.
   Unit-tested (make test).
4. Split + live preview. **`e` enters Split** (reserved; not yet wired).
5. Search / TOC / file-watch / atomic save / clipboard parity.

Not yet wired: **save to disk** (Ctrl-S) lands in slice 5 with atomic write;
for now edits live in memory and reflect in Reader on Esc, but are lost on quit.

## Build & run

```sh
cd glance-c
make                                    # builds glance + glance-render
make test                               # editor unit tests (ASan/UBSan)
./glance ../testdata/sample.md          # Reader TUI; press e to edit, Esc back
./glance-render -w 80 ../testdata/sample.md   # render-only; -l light, stdin ok
```

## Known limitations / TODO

- **Source-line map:** md4c 0.5.2 does NOT expose source byte-offsets in its
  callbacks, so the `Line.source_line` field is 0 for now. Cursor sync (slice
  3/4) will fill it by a separate block-scan correlating headings/blocks to
  source lines (like Go `toc.go`), not "for free" from the parser.
- Display width is approximate (UTF-8 lead bytes = 1 col). Real wide-char/
  grapheme width via notcurses' cell model is a follow-up; the renderer's
  wrap math should consume that width.
- Tables are not column-aligned yet (bold cells separated by spaces).
- No syntax highlighting inside code blocks (styled background only).
- Link URLs are not shown after the text yet (only the link text is styled).
- Inline images / setext-neutralizing preprocessing not ported yet.
