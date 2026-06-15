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
src/tui.h/.c      notcurses Reader: blit Doc → cells, scroll  [DONE — slice 2]
src/main.c        glance TUI entry: load file/stdin, run tui   [DONE]
src/main_render.c glance-render CLI entry (render-only)        [DONE]
-- planned --
src/app.c/.h      mode state machine once >1 mode exists
src/editor.c      line-array editor widget (Insert/Split)
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
   resize re-render. Keys: q/Esc quit, j/k + arrows, Ctrl-D/U half-page,
   PgUp/PgDn + Ctrl-F/B page, g/Home top, G/End bottom.
3. Editor + Insert mode (line-array editor, Unicode-aware).
4. Split + live preview.
5. Search / TOC / file-watch / atomic save / clipboard parity.

## Build & run

```sh
cd glance-c
make
./glance ../testdata/sample.md          # Reader TUI (needs a real terminal)
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
