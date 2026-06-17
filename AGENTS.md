# AGENTS.md

Guidance for AI coding agents (and the humans driving them) working in this
repository. If you are an agent, read this before making changes — it captures
the conventions and invariants that are not obvious from the code alone.

> glance is itself a tool for agents, with two faces over the same files. As a
> **reader** of any vault: `glance --context "Q" DIR --budget N` for a token-cheap
> ranked bundle, `glance --section "FILE#Heading"` for one section, `glance
> --neighbors`/`--backlinks`/`--outline`/`--graph` for structure — all JSON on
> stdout, no server or embeddings. As a **writer**: `glance --edit` and
> `--set-frontmatter` make atomic, structure-addressed edits. `glance mcp` exposes
> all of it over MCP. See [docs/DESIGN.md](docs/DESIGN.md) and
> [docs/MCP.md](docs/MCP.md).

## What this project is

**glance** is a terminal Markdown reader/editor for macOS, written in C, that
doubles as an **agent-native memory layer** over a Markdown vault. It parses
Markdown with [md4c](https://github.com/mity/md4c) into a structured `Doc` and
consumes that one model three ways: an ANSI string (`glance-render` CLI + tests),
[notcurses](https://github.com/dankamongmen/notcurses) cells (the `glance` TUI:
Reader / Insert / Split), and **bounded projections** for agents (outline /
section / abstract). On top sit vault navigation (wikilinks, backlinks, graph),
budget-aware retrieval, an MCP server, and a surgical write API.

The source of truth is the C app in [`src/`](src/). The repository began as a Go
program; that implementation now lives only in git history, at the `go-final`
tag — there is no Go code in the working tree.

## Build, test, run

```sh
make            # build ./glance (TUI) and ./glance-render (CLI)
make test       # run every unit test under AddressSanitizer + UBSan (23 suites)
make clean      # remove binaries and build artifacts

./glance testdata/sample.md                  # try the reader interactively
./glance --context "rendering" testdata/vault --budget 120   # try retrieval
```

Dependencies are `md4c`, `notcurses`, and `pkg-config` (`brew install md4c
notcurses pkg-config`). The TUI needs a real terminal, so verify TUI changes by
hand; everything testable without a terminal has a unit test, and **new behaviour
must come with one**. `make test` must stay green and ASan/UBSan-clean.

> **Gotcha:** do not run `./glance …` as a background command or with `&` — a
> backgrounded glance spins at 100% CPU in some sandboxes (job-control / no
> controlling tty). Run agent CLIs synchronously; each finishes in well under a
> second.

## Conventions (please follow exactly)

- **Documentation style.** Every function is preceded by a `/* ... */` comment in
  **English** stating what it does. Inline comments only when strictly necessary;
  the code should read cleanly on its own. Match the surrounding style.
- **Keys are vi-style.** `i` enters Insert, `e` enters Split, `Esc` returns to
  Reader. Do not swap these.
- **No backtick / code-fence auto-pairing.** Brackets `[ ( {` auto-close; the user
  types fences by hand.
- **Commits credit the author only.** Do not add `Co-Authored-By` or any
  agent/assistant trailer to commits in this repo.
- **Keep the docs current.** Non-trivial changes update [`STATUS.md`](STATUS.md)
  (module map, feature list) and [`context.md`](context.md) (current status) in the
  same change; agent-layer changes also update [`docs/DESIGN.md`](docs/DESIGN.md).
  Skip only for typo-level edits.

## Architecture in one screen

The renderer (`render.c`) turns Markdown into a `Doc` (visual `Line`s of styled
`Run`s). Everything else consumes that model.

```
render.c     md4c -> structured Doc (lines of styled runs); headings/links tagged
doc_ansi.c   Doc -> ANSI string (the render CLI and tests)
preprocess.c tolerant-Markdown fix-ups before parsing (bold tighten, setext)
search.c     case-insensitive full-text search over a Doc
toc.c        table of contents from the tagged heading lines
editor.c     rune-aware line-array text buffer
completion.c bracket auto-pairing
highlight.c  spec-driven per-language code highlighter
image_size.c pixel dimensions from an image header (aspect-ratio sizing)
fs_save.c    atomic write (temp file + rename, preserving mode)
fswatch.c    kqueue watch of the parent directory (survives atomic rename)
clipboard.c  pbcopy + open (system clipboard / opening links)
vault.c      vault scan + wikilink resolution (the folder is the vault)
graph.c      the vault link graph (shared by --graph and the Ctrl-G explorer)
─ agent-memory layer ─
section.c    heading anchor -> subtree + abstract projection (bounded reads)
receipt.c    token-cost estimate + saved-% receipt
bm25.c       Okapi BM25 lexical ranking index (the retrieval core)
embed.c      embedding seam: Embedder interface + a hashing default + cosine
context.c    budget planner: score order, diversity, coarse-to-fine, manifest
edit.c       surgical source edits: section append/insert/replace, frontmatter
json.c       a small dependency-free JSON parser (for the MCP server)
mcp.c        MCP server over stdio (JSON-RPC 2.0): the agent-memory tools
agent.c      the JSON exports + the retrieval/write orchestration
util.c       shared UTF-8 + whole-file helpers
tui.c        notcurses front-end: modes, input, drawing, event loop
main.c       glance entry (TUI)   ·   main_render.c   glance-render entry (CLI)
```

Key invariants worth preserving:

- **One model, sinks.** Add rendering features in `render.c` (the `Doc`), not in a
  sink, so the CLI, TUI, agent projections, and tests stay consistent.
- **The vault is a folder.** No init, no index. Root is found by walking up to a
  `.git`/`.obsidian` marker; the scan is recursive.
- **The MCP tools reuse the exact CLI exports** (captured via a stdout redirect),
  so the CLI and MCP surface never drift — change behaviour in `agent.c`, not in
  two places.
- **The untrusted-input boundary is the MCP server.** `json.c` caps parse depth;
  `mcp.c` validates UTF-8 on output. Keep new MCP input paths defensive.
- **Cursor sync is proportional / legacy keyboard mode is intentional** — md4c
  exposes no source byte-offsets, and the TUI disables the kitty keyboard protocol
  at init to avoid an escape-leak on exit. Neither is a bug to "fix" naively.

## Good first tasks

Open items live in [`STATUS.md`](STATUS.md), [`context.md`](context.md), and the
review report [`docs/REVIEW.md`](docs/REVIEW.md). Agent-layer next steps (see
DESIGN.md §11): a MiniLM-class semantic encoder behind the `Embedder` interface
(gated on an on-device benchmark), a persistent `.glance/` index cache, and
graph-expansion retrieval. User-side residuals: image caching, wide-table
wrapping, wide-character display widths.
