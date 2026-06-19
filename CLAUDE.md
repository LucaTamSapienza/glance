# CLAUDE.md

This file provides guidance to Claude Code when working with code in this repository.

## What this project is

**glance** — a terminal Markdown reader/editor for macOS, written in **C**, that
doubles as an **agent-native memory layer** over a Markdown vault. Two faces over
the same files:

- **User-side** — a rendered TUI with three modes: Reader (rendered preview with a
  block cursor), Insert (full-screen raw editor), Split (editor + live preview).
  Plus vault navigation: `[[wikilinks]]`, backlinks, a graph explorer, themes.
- **Agent-side** — token-cheap bounded reads, budget-aware retrieval with a token
  receipt, an MCP server (`glance mcp`), and a surgical write API, so an agent
  reads and maintains the vault for a fraction of the tokens. Specced in
  [`docs/DESIGN.md`](docs/DESIGN.md).

The renderer is owned end-to-end: md4c parses Markdown into a structured `Doc`
(lines of styled runs), which the sinks consume — an ANSI serializer, a notcurses
cell blitter, and the agent layer's bounded projections. No glamour, no black box.

> The repository was originally a Go program; that implementation now lives only
> in git history, at the `go-final` tag (`git checkout go-final`). All work
> happens in `src/`.

## Commands

```sh
make                 # build ./glance (TUI) and ./glance-render (CLI)
make test            # all unit tests under AddressSanitizer + UBSan
make install         # copy both binaries to $(PREFIX)/bin (default /usr/local)
make clean           # remove binaries and build artifacts

./glance --help                         # full usage + every key binding (both sides)
./glance testdata/sample.md             # user-side: open in the TUI
./glance-render -w 80 testdata/sample.md   # render to ANSI on stdout (-l = light)
cat note.md | ./glance                  # read from stdin

# agent-side (JSON on stdout; retrieval carries a token receipt)
./glance --context "Q" ./testdata/vault --budget 4000   # budgeted retrieval bundle
./glance --section "file.md#Heading"    # one section + receipt
./glance --outline file.md --depth 2 --abstract
./glance --edit file.md append "Tasks" "- new"          # surgical write (atomic)
./glance mcp                            # MCP server over stdio (Claude Desktop, etc.)
```

Requires `md4c`, `notcurses`, `pkg-config` (`brew install md4c notcurses pkg-config`).

> **Gotcha:** never run `./glance …` as a background command or with `&` — a
> backgrounded glance spins at 100% CPU in some sandboxes (no controlling tty).
> Run agent CLIs synchronously; each finishes in well under a second.

## Architecture

```
src/
  render.h/.c    md4c -> structured Doc (lines of styled runs); headings/links tagged
  doc_ansi.c     Doc -> ANSI string (render CLI + tests)
  preprocess.c   tolerant-Markdown fix-ups before parse (bold tighten, setext)
  search.c       case-insensitive full-text search over a Doc
  toc.c          table of contents from the tagged heading lines
  editor.c       rune-aware line-array text buffer
  completion.c   bracket auto-pairing (no backtick/fence pairing)
  highlight.c    spec-driven per-language code highlighter (token classes)
  image_size.c   pixel dimensions from an image header (aspect-ratio sizing)
  fs_save.c      atomic write (temp file + rename, preserve mode)
  fswatch.c      kqueue watch of the parent directory
  clipboard.c    pbcopy + open (system clipboard / opening links)
  vault.c        vault scan + wikilink resolution (the folder is the vault)
  graph.c        vault link graph (shared by --graph and the Ctrl-G explorer)
  # agent-memory layer:
  section.c      heading anchor -> subtree + abstract projection (bounded reads)
  receipt.c      token-cost estimate + saved-% receipt
  bm25.c         Okapi BM25 lexical ranking index (the retrieval core)
  embed.c        embedding seam: Embedder interface + a hashing default + cosine
  context.c      budget planner: score order, diversity, coarse-to-fine, manifest
  edit.c         surgical source edits: section append/insert/replace, frontmatter
  json.c         a small dependency-free JSON parser (for the MCP server)
  mcp.c          MCP server over stdio (JSON-RPC 2.0): the agent-memory tools
  agent.c        JSON exports + retrieval/write orchestration (all --… subcommands)
  util.c         shared UTF-8 + whole-file helpers
  tui.c          notcurses front-end: modes, input, drawing, event loop
  main.c         glance entry (TUI)    ·   main_render.c   glance-render entry (CLI)
tests/           one unit test per pure module (make test — 27 suites)
testdata/        sample.md showcase + an example vault/
docs/            DESIGN.md (agent-memory north-star) · MCP.md · HANDOFF.md · REVIEW.md
```

## Design notes & invariants

- **One model, many sinks.** The renderer emits a structured `Doc`; `doc_ansi.c`,
  `tui.c`, and the agent layer's projections (`section.c`/`context.c`) consume it.
  Add rendering features in `render.c`, not in a sink, so the CLI, TUI, agent
  reads, and tests stay consistent.
- **MCP tools reuse the exact CLI exports.** `mcp.c` runs the same `agent.c`
  functions and captures their stdout, so the CLI and MCP surface never drift —
  change behaviour in `agent.c`, never in two places.
- **The untrusted-input boundary is the MCP server.** `json.c` caps parse depth
  and `mcp.c` validates UTF-8 on output; keep new MCP input paths defensive
  (see `docs/REVIEW.md`).
- **The vault is a folder.** No `--init`, no index. `vault.c` finds the root by
  walking up to a `.git`/`.obsidian` marker and scans it recursively; bare
  `[[names]]` resolve by stem across the whole tree.
- **Cursor sync is offset-based and exact.** md4c hands text pointers that index
  into the (preprocessed) source, so `render.c` records each visual line's source
  line during the parse (`src_line_at` → `Line.source_line`); `preprocess_map`
  recovers the original line across the blank lines preprocessing may insert. It
  is exact wherever a source line maps to its own visual line; consecutive lines
  md4c merges into one paragraph (soft break) share the first line's number — the
  inherent limit of a *rendered* preview, not a fixable bug.
- **File watcher watches the parent directory**, not the file, because atomic
  saves use `rename`, which would break an inode-level watch.
- **Legacy keyboard mode is intentional.** `tui.c` disables the kitty keyboard
  protocol at init (`term_kbd_reset`); this fixes an escape-sequence leak on exit
  in some terminals. Don't re-enable enhanced keyboard modes without testing exit.
- **`atomic_write` is never called with an empty path** (stdin input has no path;
  the UI prevents saving in that case).

## Conventions (non-negotiable)

- **Documentation style.** Every function is preceded by a `/* ... */` comment in
  **English** stating what it does. Inline comments only when strictly necessary;
  the code should read cleanly on its own. New behaviour ships with a unit test,
  and `make test` must stay green and ASan/UBSan-clean.
- **vi-style keys:** `i` = Insert, `e` = Split, `Esc` = Reader. Do not swap.
- **No backtick / code-fence auto-pairing.** Brackets `[ ( {` auto-close; fences
  are typed by hand.
- **Commits credit the author only** — never add a `Co-Authored-By` trailer in
  this repo.

## Maintaining project status

Whenever you make non-trivial changes (new behavior, bug fixes, refactors, new
files, retired files), update these in the same change so a future session can
pick up the work without prior conversation context:

- `context.md` — keep the **Current status** section accurate (what's done,
  what's open) and update the date stamp at the top.
- `STATUS.md` — keep the module map and feature list (user-side / agent-side) current.
- `docs/DESIGN.md` — for agent-layer changes, keep the milestone status and open
  questions (§9/§11) current.
- The user's auto-memory `project_glance_c_autonomous_plan.md` — keep the plan,
  structure, and known-gaps sections current.

Skip the update only for tiny, fully self-contained edits (a typo, a comment
tweak). For anything bigger, update the status docs in the same edit.
