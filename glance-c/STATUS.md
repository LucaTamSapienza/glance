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

## Module map (Go → C, planned)

```
src/main.c        cmd/glance: CLI, load, notcurses init, event loop
src/app.c/.h      internal/app model: modes + Update state machine
src/render_md.c   THE renderer: md4c → styled output   [DONE — slice 1]
src/preprocess.c  tolerant markdown (bold tighten, setext neutralize)
src/toc.c  search.c  completion.c  editor.c  viewport.c
src/fswatch.c (kqueue)  save.c (atomic write)  clipboard.c  util.c
```

## Slices

1. **Renderer (DONE)** — `md4c → ANSI`, standalone `glance-render` CLI. No TUI
   dep. Handles headings, bold/italic/code/strike/underline spans, links,
   ordered/unordered/nested lists, blockquotes, fenced code blocks, tables,
   thematic breaks, word-wrap. Clean under ASan/UBSan across all testdata.
2. Viewport + Reader mode (needs `brew install notcurses`).
3. Editor + Insert mode (line-array editor, Unicode-aware).
4. Split + live preview.
5. Search / TOC / file-watch / atomic save / clipboard parity.

## Build & run (slice 1)

```sh
cd glance-c
make
./glance-render -w 80 ../testdata/sample.md   # -l for light theme, stdin ok
```

## Known limitations / TODO in the renderer

- Display width is a slice-1 approximation (counts UTF-8 lead bytes as 1 col);
  real wide-char/grapheme width arrives with notcurses in slice 2.
- Tables are not column-aligned yet (plain bold cells separated by spaces).
- No syntax highlighting inside code blocks (styled background only).
- SGR persists across soft-wrapped lines rather than being re-asserted per
  line; fine for stdout, will be revisited when emitting to a notcurses plane
  (cell-based, so the ANSI-string model goes away there).
- Link URLs are not shown after the text yet (only the link text is styled).
- Inline images / setext-neutralizing preprocessing not ported yet.
