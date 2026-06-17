# glance — Project Context

> Orientation for anyone (including Claude) picking up the work.
> Last updated: 2026-06-17 (feature/legend merged with main: legend sidebar,
> scroll/progress HUD, themes + picker, Claude plugin — atop main's heading chip,
> editor soft-wrap, charwise selection, and exact cursor sync).

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

The agent-era direction — glance as the **agent-native memory layer** over a
Markdown vault (token-cheap bounded reads, budget-constrained hybrid retrieval
with provenance, an MCP server, and a surgical write API) — is specced in
[`docs/DESIGN.md`](docs/DESIGN.md). That is the north-star roadmap; the
milestones there (M1 `context`+receipt → M2 MCP → M3 semantic → M4 write) drive
the next phase of work.

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

## Current status (2026-06-17)

**Agent-memory layer — M1 shipped (branch `docs/design-agent-memory`, PR #9).**
The first milestone of the [`docs/DESIGN.md`](docs/DESIGN.md) roadmap: token-cheap,
bounded JSON exports that let an agent read a vault for a fraction of the tokens.
New pure, unit-tested modules — `section.c` (anchor → subtree + abstract),
`receipt.c` (token receipt), `bm25.c` (BM25 lexical core), `context.c` (the
budget planner: score order, diversity, coarse-to-fine, truncation manifest) —
plus the `agent.c` exports `--section`, `--context "Q" DIR --budget N`,
`--neighbors`, `--backlinks --context`, `--since`, and `--outline --depth
--abstract`. `glance --context "…" DIR --budget N` is the wedge: it returns
`{query,budget_tokens,chunks,truncated,receipt}` with a token receipt (on the
test vault, ~78% saved vs reading everything).

**MCP server — M2 shipped (branch `feat/m2-mcp-server`).** `glance mcp` serves
the M1 reads as native MCP tools over stdio JSON-RPC 2.0 (Claude Desktop /
Cursor / SDK). New modules: `json.c` (a small dependency-free JSON parser) and
`mcp.c` (the server — `initialize`/`tools/list`/`tools/call`, eight `vault_*`
tools whose bodies reuse the exact `agent.c` exports, captured via a stdout
redirect). Unit-tested (`json_test`, `mcp_test`); wiring + tool reference in
[`docs/MCP.md`](docs/MCP.md). Next: M3 (semantic, behind a flag), M4 (surgical
write API).

**Post-merge fixes (theme discoverability + selection bar).** Two follow-ups
after `feature/legend` landed. (1) The `T` theme picker existed but was
undocumented: it is now listed in `--help` (KEYS — Reader), in the `?` legend
sidebar (a new **View** group with `T → themes` and `? → this legend`), and in
the Reader status bar (`T theme  ? keys`); `--help` also gains the `--theme NAME`
and `--list-themes` CLI lines. (2) The selection bar in the TOC, theme picker,
and backlinks panels bled onto the row below: each row was space-filled *before*
its colours were set, so the row under the selected one inherited the leftover
orange background. Fixed by setting the row colours before the fill in
`draw_toc`/`draw_themepick`/`draw_backlinks`, which also makes the highlight a
clean full-width bar. The graph explorer was unaffected (it never fills rows).

**Claude Code plugin (the repo is the plugin).** `.claude-plugin/plugin.json` +
`marketplace.json`, four `commands/*.md` (`/glance-outline|links|graph` wrap the
JSON exports, `/glance-preview` runs `glance-render` inline), and two skills:
`navigating-markdown-vaults` (Claude uses glance's exports) and
`previewing-markdown-with-glance` (Claude proactively offers to render markdown
for the user — inline `glance-render` or the `glance` TUI). Skill-driven, no
settings.json changes; commands shell out to the installed `glance` (hence
`make install`). Manifests validated as JSON; each wrapped command verified
against `testdata/vault/`. Not yet live-installed in a Claude session (needs
`marketplace add`). Plan: `docs/superpowers/specs/2026-06-17-claude-plugin-plan.md`.

**Color themes.** A new `src/theme.c` owns a flat `Theme` palette: 8 built-ins
(`auto-dark`, `auto-light`, `dracula`, `nord`, `gruvbox-dark`, `solarized-dark`,
`solarized-light`, `github-light`) whose document colors are authored and whose
chrome is derived from polarity + accents. `render.c` reads the palette via a new
`render_doc_themed(...)` entry; `render_doc`/`render_doc_at` are thin shims
mapping their `dark` flag to `theme_auto()`, so `agent.c` and all tests are
untouched. The TUI resolves a theme from `--theme <name>`, then `~/.config/glance/
config` (`theme = NAME`, plus `[theme:NAME]` / `base =` / `key = #RRGGBB`
overrides), else `auto` (terminal-detected). **`T`** opens a live picker
(preview-as-you-browse; `Enter` keeps and persists the choice to the config via
the pure `theme_config_set_default` + `atomic_write`, `Esc` reverts);
`--list-themes` prints the names. Reader
chrome (status bar, legend, TOC, backlinks, selection, hits, divider, progress,
cursor) reads the theme. Page background stays the terminal default by design.
Pure logic is unit-tested (`theme_test.c`); `auto-dark`/`auto-light` reproduce
the previous hardcoded palettes exactly (no visual regression).

**Trackpad scrolling + reading-progress HUD (Reader).** Mouse/trackpad wheel
events now scroll the reader (`notcurses_mice_enable(NCMICE_BUTTON_EVENT)`,
disabled again in `shutdown_tui`); the block cursor rides along so it stays
on-screen (`progress_scroll`). A thin top-right HUD shows a `NN%` reading
percentage plus a dots-ring spinner (`◜◠◝◞◡◟`) that animates while scrolling and
does a subtle ~320 ms spin-down (an ~80 ms-tick poll timeout in the event loop)
before resting on `◌`. Pure logic (percent, ride-along step, spinner frames) is
in `src/progress.c` (`progress_test.c`); `tui.c::draw_progress` draws the HUD.
Scope is Reader-only for v1.

**Hidable key-legend sidebar (Reader).** `?` toggles a rounded panel on the right
listing the reader's bindings; the document **reflows** into the narrower column
(it does not overlay), and the frame carries a persistent `Esc · ? close` hint.
`Esc` or `?` closes it. The pure layout logic — the content/panel width split,
the too-narrow→overlay fallback, and aligned `key → action` row formatting —
lives in `src/legend.c` (unit-tested in `legend_test.c`); `tui.c::draw_legend`
does the notcurses drawing, and `content_cols()` feeds the reduced width to
`rerender()` and the reader draw paths. On a window narrower than
`LEGEND_W + LEGEND_MIN_CONTENT` columns, `?` falls back to the old centered
overlay. Scope is Reader-only by design: `?` is literal text in Insert/Split.

**Merged from `main` (parallel work by the repo owner).** This branch now also
carries main's heading chip behind level-1/2 headings (woven into the themed
renderer via `heading_bg`), editor soft-wrap, charwise visual selection (`v` vs
`V`), exact offset-based reader↔editor cursor sync, the security/usability
hardening pass, and `--help`.

**Go→C migration complete.** The C app is the repository's only source, at the
**root** (`src/`, `tests/`, `Makefile`). The original Go program was tagged
**`go-final`** (recoverable with `git checkout go-final`) and then removed from
the working tree once the C version had been tested in daily use — there is no Go
code left in the repo. All docs (README, CLAUDE.md, STATUS.md, AGENT_FEATURES.md,
AGENTS.md) describe the C app. Build is clean and **all fifteen unit-test
suites pass** under ASan/UBSan (the latest are `legend_test.c`,
`progress_test.c`, and `theme_test.c`).

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
4. **Cursor sync** — offset-based: md4c text pointers index the preprocessed
   source, so `render.c` records each visual line's source line during the parse
   (`src_line_at` → `Line.source_line`), with `preprocess_map` recovering the
   original line across inserted blanks; `tui.c` uses it for an exact map.

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

**Latest (merged to `main`, PRs #5 & #6).** Two presentation features landed
after the pre-merge audit:

- **Heading chip** — level-1/2 headings (`#`/`##`) render as a coloured chip
  hugging the text (`[ text ]`: a one-cell padded background), not a full-width
  bar. `flush_word` keeps a heading's inter-word separator spaces on the same
  background so the chip stays continuous; `heading_pad` (`render.c`) wraps each
  heading line's runs in a leading/trailing pad cell. `toc.c` trims those pad
  spaces so TOC titles stay clean. `###`+ remain plain.
- **Editor soft-wrap** (Bug 4b) — the Insert/Split editor pane wraps long
  logical lines to the pane width instead of scrolling past the border; the
  cursor and scrolling count wrapped visual rows (`eline_rows`,
  `editor_vcursor`, rewritten `editor_scroll`/`draw_editor_pane` in `tui.c`).
  Note: the apparent "padding" a user may see on first opening Split is the
  *file's own* hard line breaks (e.g. README is wrapped at ~72 cols) faithfully
  shown by the raw editor, not padding glance adds — by design.

Both feature branches were deleted post-merge; only `main` remains on the remote.
Release binaries are installed to `~/.local/bin` (on `PATH`, no sudo) via
`make install PREFIX=$HOME/.local`.

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
- Cursor sync is exact per source line; consecutive source lines md4c folds into
  one paragraph (soft break) share the first line's number — inherent to a
  rendered preview, not a mapping defect.
- The editor pane soft-wraps long lines within its width (no horizontal scroll);
  the cursor and scrolling count wrapped visual rows (`editor_vcursor`,
  `eline_rows`).
- Wide tables overflow rather than wrap/truncate.
- Display width counts one column per codepoint (wide/zero-width chars TBD).

## Where to find more

- `README.md` — install, usage, keys, layout.
- `STATUS.md` — module-by-module map and feature list.
- `AGENT_FEATURES.md` — why the vault/graph/JSON features exist (research notes).
- `AGENTS.md` — guide for running this repo with a coding agent.
- `CLAUDE.md` — build commands, invariants, conventions.
- The old Go implementation is recoverable at the `go-final` git tag.
