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
section.c      heading anchor -> subtree + abstract projection (bounded reads)
receipt.c      token-cost estimate + saved-% receipt (used vs raw-read)
bm25.c         Okapi BM25 lexical ranking index (the retrieval core)
context.c      budget planner: score order, diversity, coarse-to-fine, manifest
embed.c        embedding seam: Embedder interface + a hashing default + cosine
edit.c         surgical source edits: section append/insert/replace, frontmatter
json.c         a small dependency-free JSON parser (for the MCP server)
mcp.c          MCP server over stdio (JSON-RPC 2.0): the agent-memory tools
agent.c        JSON exports: --outline/--section/--context/--neighbors/
               --backlinks/--since/--links/--graph (the agent-memory layer)
util.c         shared UTF-8 + whole-file helpers
tui.c          notcurses front-end: modes, input, drawing, event loop
main.c         glance entry (TUI)         main_render.c  glance-render entry (CLI)
```

The renderer emits a **structured Doc**; two sinks consume it — `doc_ansi.c`
(ANSI string, for the CLI/tests) and `tui.c` (notcurses cells).

## Feature list — DONE

Full parity with the original Go app, plus the vault/agent features:

- **Three modes:** Reader (rendered, block cursor), Insert (full-screen editor),
  Split (editor + live preview). The editor soft-wraps long lines to the pane
  width; the cursor and scrolling count wrapped visual rows.
- **Search** `/` with highlight, `n`/`N` next/prev.
- **TOC** panel `t`, jump on Enter.
- **Save** atomic: `:w` `:wq` `:x`, `Ctrl-S`; `:q` refuses on unsaved, `:q!`
  discards; dirty flag.
- **Live reload** on external change (kqueue), only when clean.
- **Clipboard:** visual select — `v` charwise (h/j/k/l), `V` linewise — `y` yanks
  to the system clipboard.
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
- **Agent-memory layer (M1)** — token-cheap, bounded JSON exports for agents
  (see `docs/DESIGN.md`): `--outline` (with `--depth`/`--abstract`), `--section
  "FILE#Heading"` (a heading subtree + a token receipt), `--context "Q" DIR
  [--budget N]` (a budgeted retrieval bundle — BM25 + a graph prior, with
  diversity, coarse-to-fine projection, and a truncation manifest), `--neighbors`
  (graph neighbourhood to N hops), `--backlinks … --context` (who links here, and
  the citing line), `--since TS` (what changed). Plus the original `--links` and
  `--graph`. Each carries a token receipt where it saves tokens.
- **MCP server (M2)** — `glance mcp` serves the agent-memory reads as native
  [MCP](https://modelcontextprotocol.io) tools over stdio JSON-RPC 2.0, so Claude
  Desktop / Cursor / the Agent SDK can use a vault directly (`vault_context`,
  `vault_section`, `vault_outline`, `vault_neighbors`, `vault_backlinks`,
  `vault_since`, `vault_links`, `vault_graph`). The tool bodies reuse the exact
  CLI exports. Wiring + tool reference in `docs/MCP.md`.
- **Semantic fusion (M3, infrastructure)** — `--context --semantic` (and the MCP
  `semantic` arg) fuses an embedding cosine with the lexical BM25 score so notes
  a keyword search misses can surface; lexical stays the default. The dense
  pipeline ships behind an `Embedder` interface (`embed.c`) with a dependency-free
  feature-hashing default; a MiniLM-class encoder plugs in behind the same
  interface (gated on a latency/heat benchmark — see `docs/DESIGN.md` §11).
- **Surgical write API (M4)** — the agent declares intent + location and glance
  does the surgery: `--edit FILE append|insert|replace "Heading" "text"` splices
  into the addressed section (formatting preserved, headings inside code fences
  ignored) and `--set-frontmatter FILE KEY VALUE` updates/creates a YAML key;
  both write via `atomic_write` and are exposed as MCP `vault_edit` /
  `vault_set_frontmatter`. The agent never rewrites a whole file. `edit.c` is
  pure and unit-tested.
- **Syntax highlighting** in fenced code blocks, per language (`highlight.c`):
  C/C++, Go, Python, JS/TS, Rust, bash, YAML, JSON — keywords, strings, numbers,
  comments, function calls, shell `$vars`, and YAML/JSON keys.
- **Headings** are coloured per level; `#`/`##` (title/subtitle) also sit in a
  coloured chip — a one-cell padded background around the text (`[ text ]`),
  tinted to their hue — rather than a bar spanning the whole line.
- **Tables** are bordered and column-aligned, honouring the `:---:` markers
  (left/center/right); the table is buffered, then emitted once widths are known.
- **Inline images:** drawn in the reader via notcurses on a plane sized to the
  picture's aspect ratio, so it fills its cells with no letterbox margin — crisp
  pixel graphics (`NCBLIT_PIXEL`) where the terminal supports them, Unicode
  half-blocks otherwise — with a `▦ alt` placeholder + Enter-to-open fallback.
  `Ctrl-V` in the editor pastes a clipboard image: it saves the bytes as a PNG
  in a `<name>_media/` folder beside the document (osascript / sips) and inserts
  a `![](…)` reference.
- **Cursor sync** maps reader↔editor by exact source lines: md4c text pointers
  index the preprocessed source, so each visual line records its source line
  during the parse (`Line.source_line`); `preprocess_map` recovers the original
  line across any blank lines preprocessing inserts.

## Keys

Reader: `hjkl`/arrows move · `g`/`G` top/bottom · `Ctrl-D/U` half page · `i`
insert · `e` split · `v`/`V` char/line select (`y` yank) · `/` search (`n`/`N`) · `t`
toc · `?` key legend (sidebar) · `T` theme picker · Enter open link ·
`:w`/`:q`/`:wq`/`:q!` · `Ctrl-S` save · `Ctrl-C` quit · trackpad/wheel to scroll.
Insert/Split: type to edit, `Esc` back, `Ctrl-S` save, `Ctrl-V` paste a clipboard
image, `Alt`/`Ctrl`+`←`/`→` jump a word, `Ctrl-A`/`Ctrl-E` (and `Cmd`+`←`/`→`,
which many terminals send as those) go to line start/end. Some terminals send
`Option`+`←`/`→` as a bare `b`/`f` with no modifier — those can't be
distinguished from typed letters, so word-jump on Option+arrows needs a
terminal-side key binding (`glance --keys` shows what your terminal sends).

Opening a path that doesn't exist starts an empty buffer and creates the file on
the first `:w` / `Ctrl-S` (vim-style).

## Build & run

```sh
make                  # glance (TUI) + glance-render (CLI)
make test             # all module unit tests, ASan/UBSan
make install          # -> $(PREFIX)/bin (default /usr/local; honours PREFIX/DESTDIR)
./glance --help       # full usage + every key binding
./glance file.md
./glance-render -w 80 file.md   # -l light theme; stdin ok; --keys diagnostic
```

## Tests

Pure modules are unit-tested under ASan/UBSan (`make test`) — twelve suites:
editor, preprocess, search, toc, fs_save, completion, highlight, image_size,
render, vault, agent, graph. `editor_test` covers word-wise motion; `render_test`
covers table alignment, source-line attribution, and
the image placeholder model. The renderer is also exercised through the
`glance-render` CLI. The notcurses front-end (including image blitting) needs a
real terminal and is verified interactively.

## Known limitations / future

- **Cursor sync** is exact per source line (offset-based). The only residual is
  inherent to a rendered preview: consecutive source lines md4c folds into one
  paragraph (a soft break) render as a single visual line, so they share the
  first line's number rather than each getting their own.
- Display width counts one column per codepoint (wide/zero-width chars TBD).
- Syntax highlighting is line-by-line and best-effort (no full grammar): it
  covers common languages and may mis-tokenise exotic constructs; unknown
  languages fall back to a plain styled background.
- Inline images are sized to the picture's aspect ratio (via `image_size.c`) and
  STRETCHed to fill the plane, then blitted with `NCBLIT_PIXEL` when the terminal
  supports it (else the cell blitter). They are decoded once per frame (a decode
  cache reused one `ncvisual` across frames, which corrupted notcurses' pixel-sprite
  state and leaked escapes — so it was removed; a persistent-plane cache that moves
  planes on scroll is the correct future optimisation). They only blit when the
  image's top row is on screen, and remote (`http`) images aren't fetched. The
  cell-aspect factor is an approximation, not read from the terminal.
- Very wide tables overflow the width rather than wrapping/truncating.

## Robustness & security

Audited (static review + AddressSanitizer/UBSan fuzzing of the headless paths)
for crashes and injection. Hardened:

- **Clipboard paste** passes the file path to `osascript` as an `on run argv`
  parameter, never interpolated into the script text — a document folder named
  with a `"` or newline can no longer inject AppleScript (was a real RCE vector).
- **Vault scan** (`--graph`, `--outline`, backlinks, Ctrl-G) uses `lstat` and
  skips symlinks, plus a recursion depth cap, so a symlink cycle (`loop -> ..`)
  or a very deep tree can't drive infinite recursion / stack overflow. Regression-
  tested under ASan in `vault_test`.
- **Empty/degenerate documents**: yank, the empty-line newline split, and the
  reader↔editor cursor map all guard the zero-line / empty-buffer cases.
- **JSON exports** escape quotes/backslashes/control chars, so odd filenames
  can't break the output. Renderer run-builders no longer write `arr[n-1]` after
  a failed (OOM) push. Graph edges grow geometrically.

Residual notes: relative `../` image/link targets are not confined to the vault
(they can reference any file the user can already read); display width is one
column per codepoint.
