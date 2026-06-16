# glance — status & module map

glance is a terminal Markdown reader/editor written in C. It began as a Go
program built on glamour/glow; this is the from-scratch C rewrite that owns the
rendering instead of treating glamour as a black box. The Go original is kept,
deprecated, under [`legacy-go/`](legacy-go/). Built as small, tested,
well-documented modules.

## Why C / why own the renderer

Much of the Go code existed to fight glamour's opacity: `preprocess.go` inserted
marker paragraphs and stripped them out; `search.go` stripped ANSI to match then
re-injected SGR. Owning the renderer (md4c → our `Doc` of styled runs) makes all
of that direct: search reads run text, the TUI writes cells, the TOC is tagged
heading lines. Nothing about the output is opaque to us.

## Stack

| Concern        | Choice                                       | Replaces                     |
|----------------|----------------------------------------------|------------------------------|
| MD parse       | md4c (GFM dialect)                            | goldmark                     |
| Render         | our renderer → structured `Doc` (`render.c`) | glamour / chroma             |
| TUI runtime    | notcurses                                    | bubbletea/lipgloss/runewidth |
| Editor         | our line-array buffer (`editor.c`)           | bubbles/textarea             |
| File watch     | kqueue on parent dir (`fswatch.c`)           | fsnotify                     |
| Clipboard/open | pbcopy / open (`clipboard.c`)                | atotto/clipboard             |
| Syntax hl      | deferred (styled bg now)                     | chroma                       |

## Module map (`src/`)

```
render.h/.c    md4c -> structured Doc (lines of styled runs); links tagged
doc_ansi.c     Doc -> ANSI string (render CLI + tests)
preprocess.c   tolerant-Markdown fix-ups (bold tighten, setext neutralize)
search.c       case-insensitive full-text search over a Doc
toc.c          table of contents from tagged heading lines
editor.c       line-array text buffer with a rune-aware cursor
completion.c   bracket auto-pairing (no backtick/fence)
fs_save.c      atomic write (temp + rename, preserve mode)
fswatch.c      kqueue watch of the parent directory
clipboard.c    pbcopy + open (system clipboard / link opening)
vault.c        vault scan + wikilink resolution (the folder is the vault)
graph.c        the vault link graph (shared by --graph and the Ctrl-G explorer)
agent.c        --outline / --links / --graph JSON exports
util.c         shared UTF-8 + whole-file helpers
tui.c          notcurses front-end: modes, input, drawing, event loop
main.c         glance entry (TUI)         main_render.c  glance-render entry (CLI)
```

The renderer emits a **structured Doc**; two sinks consume it — `doc_ansi.c`
(ANSI string, for the CLI/tests) and `tui.c` (notcurses cells).

## Feature list — DONE

Full parity with the original Go app, plus the vault/agent features:

- **Three modes:** Reader (rendered, block cursor), Insert (full-screen editor),
  Split (editor + live preview).
- **Search** `/` with highlight, `n`/`N` next/prev.
- **TOC** panel `t`, jump on Enter.
- **Save** atomic: `:w` `:wq` `:x`, `Ctrl-S`; `:q` refuses on unsaved, `:q!`
  discards; dirty flag.
- **Live reload** on external change (kqueue), only when clean.
- **Clipboard:** `v`/`V` visual-line select, `y` yank to system clipboard.
- **Open links** under the cursor with Enter.
- **Tolerant Markdown** preprocessing.
- **Help** overlay `?`.
- **Auto dark/light** from the terminal background.
- **Bracket auto-pairing** in the editor.
- **Vault navigation:** `[[wikilinks]]` resolve and follow across subfolders;
  back-stack (`-` / `Ctrl-O`); backlinks panel (`b`); graph explorer (`Ctrl-G`).
- **Agent exports:** `glance --outline|--links|--graph` print JSON to stdout.

## Keys

Reader: `hjkl`/arrows move · `g`/`G` top/bottom · `Ctrl-D/U` half page · `i`
insert · `e` split · `V` visual select (`y` yank) · `/` search (`n`/`N`) · `t`
toc · `?` help · Enter open link · `:w`/`:q`/`:wq`/`:q!` · `Ctrl-S` save ·
`Ctrl-C` quit. Insert/Split: type to edit, `Esc` back, `Ctrl-S` save.

## Build & run

```sh
make                  # glance (TUI) + glance-render (CLI)
make test             # all module unit tests, ASan/UBSan
./glance file.md
./glance-render -w 80 file.md   # -l light theme; stdin ok; --keys diagnostic
```

## Tests

Pure modules are unit-tested under ASan/UBSan (`make test`) — nine suites:
editor, preprocess, search, toc, fs_save, completion, vault, agent, graph. The
renderer is additionally exercised through the search/toc/agent tests and the
`glance-render` CLI. The notcurses front-end needs a real terminal and is
verified interactively.

## Known limitations / future

- **Cursor sync is proportional**, not an exact rendered<->source 1:1 line map:
  md4c 0.5.2 exposes no source byte-offsets, so the Go app's proportional
  approach is used. Exact mapping needs a source-tracking pass.
- Display width counts one column per codepoint (wide/zero-width chars TBD).
- No syntax highlighting inside code blocks (styled background only).
- Tables aren't column-aligned; inline images not rendered (notcurses can —
  a candidate feature).
