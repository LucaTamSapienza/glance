# glance — Project Context

> Orientation for anyone (including Claude) picking up the work.
> Last updated: 2026-06-16 (C-at-root migration; highlighting, tables, images, cursor sync).

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

**Go→C migration complete (staged).** The C app is now the repository's primary
source, at the **root** (`src/`, `tests/`, `Makefile`). The original Go program
was tagged **`go-final`** and moved, deprecated and unmaintained, to
**`legacy-go/`** — it stays only as a reference and will be removed in a later
phase, after the C version has been tested in daily use. All docs (README,
CLAUDE.md, STATUS.md, AGENT_FEATURES.md, and the new AGENTS.md) were rewritten to
describe the C app and the new layout. Build is clean and **all ten unit-test
suites pass** under ASan/UBSan.

**All four renderer gaps closed.**

1. **Syntax highlighting** — per-language token coloring via a spec-driven
   highlighter (`src/highlight.c`): C/C++, Go, Python, JS/TS, Rust, bash, YAML,
   JSON. `render.c` resolves the fence language and pushes one styled run per
   token over the code background. Unknown languages keep the plain box.
2. **Inline images** — an image span becomes a placeholder block (`▦ alt`,
   linked, reserved rows) in `render.c`; the reader (`tui.c::blit_image`) decodes
   it with notcurses ncvisual and draws it over the rows (pixel or half-blocks),
   degrading to the placeholder for URLs / missing files / plain terminals.
3. **Table alignment** — tables are buffered whole, then emitted bordered and
   column-aligned (left/center/right from md4c's `MD_BLOCK_TD_DETAIL`).
4. **Cursor sync** — `render.c::tag_source_lines` attributes each visual line to
   a source line by content (forward, monotonic); `tui.c` uses `Line.source_line`
   for an exact reader↔editor map, with the proportional map as fallback.

All covered by `render_test.c` (+ `highlight_test.c`); 11 test suites total.

Branch `master`, pushed to `git@github.com:LucaTamSapienza/glance.git` (the
default branch is `master`; there is no `main`). Backup branches retained:
`c-rewrite` (Go-parity snapshot), `c-agent-features`, `feat-graph-view`.

### Feature parity — done

All original Go features are ported: three modes, search (`/` `n` `N`), TOC
(`t`), atomic save (`:w`/`Ctrl-S`), kqueue live-reload, clipboard yank
(`v`/`V`/`y`), open-link (Enter), tolerant-Markdown preprocessing, help (`?`),
auto dark/light, bracket auto-pairing. Plus the agent-era features: wikilinks +
cross-file navigation with a back-stack, backlinks panel (`b`), graph explorer
(`Ctrl-G`), and the `--outline`/`--links`/`--graph` JSON exports.

## Known gaps / open items

The four gaps the user prioritised are all **done** (see above). Residual polish:

- Syntax highlighting: more languages, multi-line raw strings (Go backticks),
  better YAML scalar typing.
- Images: fixed reserved height (no aspect sizing), decoded per frame (no cache),
  hidden until the top row scrolls in, and remote (`http`) images aren't fetched.
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
- `legacy-go/` — the deprecated Go implementation (do not modify).
