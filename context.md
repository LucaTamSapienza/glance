# glance — Project Context

> Orientation for anyone (including Claude) picking up the work.
> Last updated: 2026-06-16 (aspect-tight pixel images; open a non-existent path
> as a new empty file).

## What it is

**glance** is a terminal Markdown reader/editor for macOS, written in **C**. It
opens `.md` files (or stdin) in a rendered TUI with three modes:

- **Reader** — the rendered Markdown preview, with a navigable block cursor.
- **Insert** — a full-screen raw-text editor.
- **Split** — the raw editor on the left, a live rendered preview on the right.

Beyond reading one file, glance navigates a *vault*: it resolves `[[wikilinks]]`
across subfolders, shows backlinks, has a graph explorer (`Ctrl-G`), and exports
a document's structure as JSON for agents (`--outline`, `--links`, `--graph`).

## Vision

The north-star is a **WYSIWYG Markdown editor — Notion or Obsidian — but entirely
in the terminal**, that serves the human at the prompt *and* the agent reading
the same files. The C rewrite exists so glance owns its rendering (md4c → our own
`Doc` → ANSI/cells) instead of treating glamour/glow as a black box, which is what
the original Go version did.

## How to build and run

```sh
make                 # build ./glance (TUI) and ./glance-render (CLI)
make test            # all unit tests under AddressSanitizer + UBSan
./glance testdata/sample.md
./glance-render -w 80 testdata/sample.md   # -l light; stdin ok; --keys diagnostic
./glance --graph testdata/vault            # vault link graph as JSON
```

Dependencies: `md4c` and `notcurses` (`brew install md4c notcurses`).

## Architecture

The renderer (`src/render.c`) turns Markdown into a `Doc` — a list of visual
lines, each a sequence of styled runs — and two sinks consume it: `doc_ansi.c`
(ANSI string, for the CLI and tests) and `tui.c` (notcurses cells). See
`STATUS.md` for the full module map and `CLAUDE.md` for the invariants.

## Current status (2026-06-16)

**Go→C migration complete.** The C app is the repository's only source, at the
**root** (`src/`, `tests/`, `Makefile`). The original Go program was tagged
**`go-final`** (recoverable with `git checkout go-final`) and then removed from
the working tree once the C version had been tested in daily use — there is no Go
code left in the repo. All docs (README, CLAUDE.md, STATUS.md, AGENT_FEATURES.md,
AGENTS.md) describe the C app. Build is clean and **all twelve unit-test suites
pass** under ASan/UBSan.

**All four renderer gaps closed.**

1. **Syntax highlighting** — per-language token coloring via a spec-driven
   highlighter (`src/highlight.c`): C/C++, Go, Python, JS/TS, Rust, bash, YAML,
   JSON. `render.c` resolves the fence language and pushes one styled run per
   token over the code background. Unknown languages keep the plain box.
2. **Inline images** — an image span becomes a placeholder block (`▦ alt`,
   linked, reserved rows) in `render.c`; the reader (`tui.c::blit_image`) decodes
   it with notcurses ncvisual and draws it over the rows (pixel or half-blocks),
   degrading to the placeholder for URLs / missing files / plain terminals.
   `Ctrl-V` in the editor pastes a clipboard image: `clip_image_save`
   (osascript / sips) writes a PNG into a `<stem>_media/` folder beside the
   document and a `![](…)` ref is inserted at the cursor.
3. **Table alignment** — tables are buffered whole, then emitted bordered and
   column-aligned (left/center/right from md4c's `MD_BLOCK_TD_DETAIL`).
4. **Cursor sync** — `render.c::tag_source_lines` attributes each visual line to
   a source line by content (forward, monotonic); `tui.c` uses `Line.source_line`
   for an exact reader↔editor map, with the proportional map as fallback.

All covered by `render_test.c` (+ `highlight_test.c`); 12 test suites total.

**Recent fixes.** Opening a non-existent path now starts an empty buffer and
creates the file on first save (was a fatal error). Word-wise editor motion
(`editor_word_left/right`) bound to Alt/Ctrl+arrows and the `Meta-b`/`Meta-f`
some terminals send for `Option`+arrows (so those no longer type stray letters);
`Cmd`+arrows go to line start/end. A segfault when entering Insert on an empty
document (`doc_src_line` indexing an empty `Doc`) is guarded. Clipboard image
paste now checks `clipboard info` and retries (~1.5s) so a single `Ctrl-V`
captures a promised pasteboard (screenshots / "copy image"), not just files.
`glance --help` (and `glance-render -h`) now print full usage + every key
binding, kept in sync with the in-app `?` overlay; `make install` (honours
`PREFIX`/`DESTDIR`) puts both binaries on `PATH`. A small renderer cleanup
exported `line_text()` so search and the TOC share one run-concatenation helper.

**Security & stability audit (pre-merge).** Static review plus ASan/UBSan fuzzing
of the headless paths (`glance-render`, `--graph`/`--outline`/`--links`) against
hostile input. Fixed: an **AppleScript injection / RCE** in the clipboard paste
(the path is now an `osascript` argv parameter, not interpolated — verified the
old form executed a payload and the new one does not); a **symlink-cycle / deep-
recursion crash** in the vault scan (`lstat` + skip symlinks + depth cap, with an
ASan regression test); an empty-document **yank** NULL-deref and an empty-line
`editor_newline` UB; and OOM-only out-of-bounds writes in the renderer's run
builders. Graph edges now grow geometrically. See STATUS.md "Robustness &
security" for the residual notes.

Default branch `main`, pushed to `git@github.com:LucaTamSapienza/glance.git`
(renamed from `master`). The old C-development snapshot branches (`c-rewrite`,
`c-agent-features`, `feat-graph-view`) were deleted once fully incorporated into
`main`; the Go original remains recoverable at the `go-final` tag.

### Feature parity — done

All original Go features are ported: three modes, search (`/` `n` `N`), TOC
(`t`), atomic save (`:w`/`Ctrl-S`), kqueue live-reload, clipboard yank
(`v` charwise / `V` linewise select, `y` yank), open-link (Enter),
tolerant-Markdown preprocessing, help (`?`),
auto dark/light, bracket auto-pairing. Plus the agent-era features: wikilinks +
cross-file navigation with a back-stack, backlinks panel (`b`), graph explorer
(`Ctrl-G`), and the `--outline`/`--links`/`--graph` JSON exports.

## Known gaps / open items

The four gaps the user prioritised are all **done** (see above). Residual polish:

- Syntax highlighting: more languages, multi-line raw strings (Go backticks),
  better YAML scalar typing.
- Images: the reader plane is sized to the picture's aspect ratio (cells ≈ 2:1
  tall:wide, from `image_size.c`) and the image is STRETCHed to fill it, so there
  is no letterbox margin. With a tight plane the crisp `NCBLIT_PIXEL` blitter is
  used on terminals that support it (detected via `notcurses_check_pixel_support`),
  falling back to the cell blitter elsewhere. Decoded per frame (no decode cache —
  reusing an ncvisual corrupted notcurses' pixel sprites; a persistent-plane cache
  that *moves* planes on scroll is the right future optimisation). Remote (`http`)
  images aren't fetched; cell-aspect ratio is approximated as a constant.
- Cursor sync: soft-wrapped multi-line paragraphs map to the block, not the exact
  wrapped sub-line (bounded by md4c having no source offsets).
- Wide tables overflow rather than wrap/truncate.
- Display width counts one column per codepoint (wide/zero-width chars TBD).

## Where to find more

- `README.md` — install, usage, keys, layout.
- `STATUS.md` — module-by-module map and feature list.
- `AGENT_FEATURES.md` — why the vault/graph/JSON features exist (research notes).
- `AGENTS.md` — guide for running this repo with a coding agent.
- `CLAUDE.md` — build commands, invariants, conventions.
- The old Go implementation is recoverable at the `go-final` git tag.
