# glance — status & module map

glance is a terminal Markdown reader/editor written in C that doubles as an
**agent-native memory layer** over a Markdown vault. It began as a Go program
built on glamour/glow; this is the from-scratch C rewrite that owns the rendering
instead of treating glamour as a black box. The Go original lives only in git
history, at the `go-final` tag. Built as small, tested, well-documented modules.

## Why C / why own the renderer

Much of the Go code existed to fight glamour's opacity: `preprocess.go` inserted
marker paragraphs and stripped them out; `search.go` stripped ANSI to match then
re-injected SGR. Owning the renderer (md4c → our `Doc` of styled runs) makes all
of that direct: search reads run text, the TUI writes cells, the TOC is tagged
heading lines, and the agent layer projects the same `Doc` at any granularity.
Nothing about the output is opaque to us.

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
| Retrieval      | BM25 + graph prior + budget (`bm25/context`) | — (new, agent-side)          |
| Embeddings     | `Embedder` seam + hashing default (`embed.c`)| — (new, opt-in)              |
| JSON / MCP     | own parser + stdio server (`json/mcp`)       | — (new, agent-side)          |

## Module map (`src/`)

```
render.h/.c    md4c -> structured Doc (lines of styled runs); links tagged
doc_ansi.c     Doc -> ANSI string (render CLI + tests)
doc_html.c     Markdown -> self-contained themed HTML (md4c SAX sink; reuses highlight.c)
export.c       export a file to HTML, or PDF via an external converter
preprocess.c   tolerant-Markdown fix-ups (bold tighten, setext neutralize)
search.c       case-insensitive full-text search over a Doc
toc.c          table of contents from tagged heading lines
editor.c       line-array text buffer with a rune-aware cursor
completion.c   bracket auto-pairing (no backtick/fence)
fuzzy.c        subsequence fuzzy match + ranking (the Ctrl-P file switcher)
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
─ agent-memory layer ─
section.c      heading anchor -> subtree + abstract projection (bounded reads)
receipt.c      token-cost estimate + saved-% receipt (used vs raw-read)
bm25.c         Okapi BM25 lexical ranking index (the retrieval core)
embed.c        embedding seam: Embedder interface + a hashing default + cosine
context.c      budget planner: score order, diversity, coarse-to-fine, manifest
edit.c         surgical source edits: section append/insert/replace, frontmatter
json.c         a small dependency-free JSON parser (for the MCP server)
mcp.c          MCP server over stdio (JSON-RPC 2.0): the agent-memory tools
agent.c        JSON exports + retrieval/write orchestration (all the --… subcommands)
util.c         shared UTF-8 + whole-file helpers
tui.c          notcurses front-end: modes, input, drawing, event loop
main.c         glance entry (TUI)         main_render.c  glance-render entry (CLI)
```

The renderer emits a **structured Doc**; the sinks consume it — `doc_ansi.c`
(ANSI string), `tui.c` (notcurses cells), and the agent layer (`section.c` /
`context.c` bounded projections).

## Feature list — DONE

### User-side (reader / editor)

- **Three modes:** Reader (rendered, block cursor), Insert (full-screen editor),
  Split (editor + live preview). The editor soft-wraps long lines to the pane
  width; the cursor and scrolling count wrapped visual rows.
- **Search** `/` with highlight, `n`/`N` next/prev. **TOC** panel `t`, jump on Enter.
- **Save** atomic: `:w` `:wq` `:x`, `Ctrl-S`; `:q` refuses on unsaved, `:q!`
  discards. **Live reload** on external change (kqueue): when the buffer is clean
  the disk version is adopted in any mode (Reader/Insert/Split), so a second
  session's edits sync through; with unsaved edits a conflict prompt is raised
  (`r` reload / `k` keep) instead of silently dropping either side.
- **Clipboard:** visual select — `v` charwise, `V` linewise — `y` yanks to the
  system clipboard. **Open links** / follow `[[wikilinks]]` with Enter.
- **Key legend sidebar** `?`: a rounded right-side panel; the document reflows
  beside it (no overlay), with a narrow-window centered-overlay fallback.
- **Trackpad/wheel scrolling** (cursor rides along) + a top-right reading-progress
  HUD (percent + a dots-ring spinner).
- **Color themes**: 8 built-ins (`auto`, `dracula`, `nord`, `gruvbox-dark`,
  `solarized-dark`/`-light`, `github-light`), `--theme`, `--list-themes`, a live
  **`T`** picker (preview-as-you-browse, persists to config), and
  `~/.config/glance/config`. Drives the document and the UI chrome.
- **Syntax highlighting** in fenced code blocks, per language (`highlight.c`):
  C/C++, Go, Python, JS/TS, Rust, bash, YAML, JSON.
- **Headings** coloured per level; `#`/`##` sit in a one-cell-padded coloured chip.
- **Tables** bordered and column-aligned (honouring `:---:` markers).
- **Inline images:** blitted in the reader on an aspect-sized plane — pixel
  graphics where supported, half-blocks otherwise, with a `▦ alt` placeholder
  fallback. `Ctrl-V` pastes a clipboard image into a `<name>_media/` folder.
- **Vault navigation:** `[[wikilinks]]` resolve/follow across subfolders;
  back-stack (`-`/`Ctrl-O`); backlinks panel (`b`); graph explorer (`Ctrl-G`);
  **fuzzy file switcher** (`Ctrl-P`) — type to filter the whole vault, ranked by
  a subsequence match (`fuzzy.c`), Enter opens.
- **Cursor sync** maps reader↔editor by exact source lines (`Line.source_line`).
- **Export:** `glance-render --html FILE` emits a self-contained, themed HTML page
  (semantic + reflowable, syntax-highlighted code, no JS/CDN); `glance --export
  FILE [OUT]` writes HTML, or a PDF when `OUT` ends in `.pdf` (HTML handed to a
  detected converter: weasyprint / wkhtmltopdf / headless Chrome).

### Agent-side (the M1–M4 memory layer)

Token-cheap, bounded JSON exports + retrieval + writes + an MCP server, all
reusing the same `Doc`. See [`docs/DESIGN.md`](docs/DESIGN.md).

- **Bounded reads:** `--outline FILE [--depth N] [--abstract]`,
  `--section "FILE#Heading"` (subtree + token receipt), `--neighbors`,
  `--backlinks … --context`, `--since TS`, plus `--links`/`--graph`.
- **Budgeted retrieval:** `--context "Q" DIR [--budget N] [--semantic]` returns
  `{query,budget_tokens,chunks,truncated,receipt}` — note sections ranked by BM25
  fused with a link-graph prior, selected with diversity and a coarse-to-fine
  projection (full → abstract), a truncation manifest, and a token receipt. Lexical
  + graph by default; `--semantic` fuses an embedding cosine (dependency-free
  embedder; MiniLM-class is a drop-in behind the `Embedder` interface).
- **Surgical writes:** `--edit FILE append|insert|replace "Heading" "text"` and
  `--set-frontmatter FILE KEY VALUE` — structure-addressed edits on the raw source
  (formatting preserved; fenced-code and setext headings handled), written via
  `atomic_write`. The agent never rewrites a whole file.
- **MCP server:** `glance mcp` — stdio JSON-RPC 2.0 exposing all of the above as
  native tools (Claude Desktop / Cursor / SDK). Wiring in [`docs/MCP.md`](docs/MCP.md).
- **Claude Code plugin:** the repo doubles as a plugin (`.claude-plugin/`,
  `commands/`, `skills/`) wrapping the exports and the renderer.

## Keys

Reader: `hjkl`/arrows move · `g`/`G` top/bottom · `Ctrl-D/U` half page · `i`
insert · `e` split · `v`/`V` char/line select (`y` yank) · `/` search (`n`/`N`) ·
`t` toc · `?` key legend · `T` theme picker · Enter open link · `b` backlinks ·
`Ctrl-G` graph · `:w`/`:q`/`:wq`/`:q!` · `Ctrl-S` save · trackpad/wheel to scroll.
Insert/Split: type to edit, `Esc` back, `Ctrl-S` save, `Ctrl-V` paste a clipboard
image, `Alt`/`Ctrl`+`←`/`→` word jump, `Ctrl-A`/`Ctrl-E` line start/end.

## Build & run

```sh
make                  # glance (TUI) + glance-render (CLI)
make test             # all module unit tests, ASan/UBSan (26 suites)
make install          # -> $(PREFIX)/bin (default /usr/local; honours PREFIX/DESTDIR)
./glance --help       # full usage + every key binding (user + agent)
```

## Tests

Pure modules are unit-tested under ASan/UBSan (`make test`) — **26 suites**:
editor, preprocess, search, toc, fs_save, fswatch, completion, fuzzy, legend, progress,
theme, highlight, image_size, render, doc_html, vault, graph; and the agent layer — receipt, bm25,
context, section, embed, json, edit, agent (JSON exports + a write roundtrip),
mcp (a full initialize → tools/list → tools/call session + the error paths). The
notcurses front-end needs a real terminal and is verified interactively.

## Known limitations / future

- **Agent-side (DESIGN.md §11):** the semantic tier ships a feature-hashing
  embedder (a structural signal, not a model); a MiniLM-class encoder is the
  drop-in upgrade behind the `Embedder` interface, gated on an on-device benchmark.
  A persistent `.glance/` index cache and graph-expansion retrieval are next.
- **Cursor sync** is exact per source line; consecutive lines md4c folds into one
  paragraph (soft break) share a line number — inherent to a rendered preview.
- Display width is one column per codepoint (wide/zero-width chars TBD).
- Inline images decode per frame (a persistent-plane cache is the right fix);
  remote (`http`) images aren't fetched; wide tables overflow rather than wrap.

## Robustness & security

Audited (static review + ASan/UBSan fuzzing of the headless paths) for crashes and
injection. Hardened:

- **Clipboard paste** passes the file path to `osascript` as an `argv` parameter,
  never interpolated — closes an AppleScript RCE vector.
- **Vault scan** uses `lstat`, skips symlinks, and caps recursion depth, so a
  symlink cycle or deep tree can't drive infinite recursion. ASan-regression-tested.
- **JSON exports** escape quotes/backslashes/control chars; renderer run-builders
  don't write after a failed (OOM) push; graph edges grow geometrically.
- **Agent-side / untrusted input (the MCP boundary, see `docs/REVIEW.md`):** the
  JSON parser caps nesting depth (closed a stack-overflow DoS); `--edit`/`vault_edit`
  reject unknown ops; `edit_frontmatter` rejects newlines and quotes YAML-unsafe
  values; `edit_section` is setext-aware (closed a REPLACE data-loss) and tracks
  ```` ``` ````/`~~~` fences distinctly; `\u` combines surrogate pairs; JSON numbers
  are grammar-validated; `emit_id`/`emit_jstr` keep output well-formed (isfinite +
  UTF-8 validation).

Residual: relative `../` image/link targets aren't confined to the vault; display
width is one column per codepoint.
