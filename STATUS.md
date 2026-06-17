# glance — status & module map

glance is a terminal Markdown reader/editor written in C. It began as a Go
program built on glamour/glow; this is the from-scratch C rewrite that owns the
rendering instead of treating glamour as a black box. The Go original lives only
in git history now, at the `go-final` tag. Built as small, tested,
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
| Syntax hl      | spec-driven highlighter (`highlight.c`)      | chroma                       |
| Images         | notcurses ncvisual blit (`tui.c`)            | images.go (detection only)   |

## Module map (`src/`)

```
render.h/.c    md4c -> structured Doc (lines of styled runs); links tagged
doc_ansi.c     Doc -> ANSI string (render CLI + tests)
preprocess.c   tolerant-Markdown fix-ups (bold tighten, setext neutralize)
search.c       case-insensitive full-text search over a Doc
toc.c          table of contents from tagged heading lines
editor.c       line-array text buffer with a rune-aware cursor
completion.c   bracket auto-pairing (no backtick/fence)
legend.c       Reader key-sidebar layout (width split, aligned row formatting)
progress.c     Reader scroll/progress HUD logic (percent, ride-along, spinner)
theme.c        color themes: built-in palettes, chrome derivation, config parser
highlight.c    spec-driven per-language code highlighter (token classes)
image_size.c   pixel dimensions from an image header (for aspect-ratio sizing)
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
- **Key legend sidebar** `?`: a rounded right-side panel of the reader's
  bindings; the document reflows beside it (no overlay) and the frame shows a
  persistent `Esc · ? close` hint. Falls back to a centered overlay when the
  window is too narrow to reflow.
- **Trackpad/wheel scrolling** in the reader (cursor rides along), with a thin
  top-right reading-progress HUD: a `NN%` percentage and a dots-ring spinner
  that animates while scrolling and spins down subtly when it stops.
- **Color themes**: 8 built-ins (`auto`, `dracula`, `nord`, `gruvbox-dark`,
  `solarized-dark`/`-light`, `github-light`), `--theme <name>`,
  `--list-themes`, a live **`T`** picker (preview-as-you-browse, `Enter` keeps &
  persists to config, `Esc` reverts), and `~/.config/glance/config` for the
  default + custom palettes. Drives both the document and the UI chrome.
- **Claude Code plugin**: the repo doubles as a plugin (`.claude-plugin/`,
  `commands/`, `skills/`). Slash commands wrap the JSON exports
  (`/glance-outline|links|graph`) and the renderer (`/glance-preview`); two
  skills let Claude *use* glance to navigate a vault and *proactively offer* to
  render markdown for the user in-session.
- **Auto dark/light** from the terminal background.
- **Bracket auto-pairing** in the editor.
- **Vault navigation:** `[[wikilinks]]` resolve and follow across subfolders;
  back-stack (`-` / `Ctrl-O`); backlinks panel (`b`); graph explorer (`Ctrl-G`).
- **Agent exports:** `glance --outline|--links|--graph` print JSON to stdout.
- **Syntax highlighting** in fenced code blocks, per language (`highlight.c`):
  C/C++, Go, Python, JS/TS, Rust, bash, YAML, JSON — keywords, strings, numbers,
  comments, function calls, shell `$vars`, and YAML/JSON keys.
- **Tables** are bordered and column-aligned, honouring the `:---:` markers
  (left/center/right); the table is buffered, then emitted once widths are known.
- **Inline images:** drawn in the reader via notcurses (pixel graphics or
  Unicode half-blocks), with a `▦ alt` placeholder + Enter-to-open fallback.
  `Ctrl-V` in the editor pastes a clipboard image: it saves the bytes as a PNG
  in a `<name>_media/` folder beside the document (osascript / sips) and inserts
  a `![](…)` reference.
- **Cursor sync** maps reader↔editor by content-attributed source lines
  (`Line.source_line`), exact at structural lines, with a proportional fallback.

## Keys

Reader: `hjkl`/arrows move · `g`/`G` top/bottom · `Ctrl-D/U` half page · `i`
insert · `e` split · `V` visual select (`y` yank) · `/` search (`n`/`N`) · `t`
toc · `?` key legend (sidebar) · Enter open link · `:w`/`:q`/`:wq`/`:q!` · `Ctrl-S` save ·
`Ctrl-C` quit · trackpad/wheel to scroll · `T` theme picker. Insert/Split: type to
edit, `Esc` back, `Ctrl-S` save, `Ctrl-V` paste a clipboard image.

## Build & run

```sh
make                  # glance (TUI) + glance-render (CLI)
make test             # all module unit tests, ASan/UBSan
./glance file.md
./glance-render -w 80 file.md   # -l light theme; stdin ok; --keys diagnostic
```

## Tests

Pure modules are unit-tested under ASan/UBSan (`make test`) — eleven suites:
editor, preprocess, search, toc, fs_save, completion, highlight, render, vault,
agent, graph. `render_test` covers table alignment, source-line attribution, and
the image placeholder model. The renderer is also exercised through the
`glance-render` CLI. The notcurses front-end (including image blitting) needs a
real terminal and is verified interactively.

## Known limitations / future

- **Cursor sync** is exact at structural lines (headings, code, list items,
  table rows, single-line paragraphs) via content attribution, but md4c 0.5.2
  exposes no source byte-offsets, so a soft-wrapped multi-line paragraph is still
  approximate (it maps to the block, not the exact wrapped sub-line).
- Display width counts one column per codepoint (wide/zero-width chars TBD).
- Syntax highlighting is line-by-line and best-effort (no full grammar): it
  covers common languages and may mis-tokenise exotic constructs; unknown
  languages fall back to a plain styled background.
- Inline images are sized to the picture's aspect ratio (via `image_size.c`) but
  decoded once per frame (a decode cache reused one `ncvisual` across frames,
  which corrupted notcurses' pixel-sprite state and leaked escapes — so it was
  removed; a persistent-plane cache is the correct future optimisation). They only
  blit when the image's top row is on screen, and remote (`http`) images aren't
  fetched. The cell-aspect factor is an approximation, not read from the terminal.
- Very wide tables overflow the width rather than wrapping/truncating.
