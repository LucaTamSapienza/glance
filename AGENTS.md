# AGENTS.md

The canonical guide for working in this repository — human or agent, whatever
tool you drive. `CLAUDE.md` imports this file; nothing written here has a
second copy anywhere else.

**Start here:** the project's living state (what's done, what's in flight,
what's open, why past choices were made) is the memory vault at
[`memory/`](memory/MEMORY.md) — read its index before non-trivial work. This
file is the *rules*; the memory is the *state*.

## Looking things up? Use glance — and quote the receipt

glance is itself a tool for agents, and this repo eats its own cooking. When
you need project information — the current state, a past decision, the
design, the docs — **query it with glance instead of reading whole files**:

```sh
./glance --context "your question" memory/ --budget 2000   # state, decisions, lessons, history
./glance --context "your question" docs/ --budget 3000     # design, MCP wiring, specs
./glance --section "memory/status.md#Open"                 # exactly one section
./glance --outline docs/DESIGN.md --depth 2 --abstract     # structure before content
```

(`make` first if `./glance` isn't built; always run it synchronously — never
in the background, see the gotcha under *Build, test, run*.)

Every retrieval carries a **token receipt** — `{used_tokens, raw_tokens,
saved_pct}`. **When you report what you found, say what the lookup saved**
(e.g. "glance: 483 of 2,281 tokens — 78% saved"). The receipt is the
product's headline number, so every answer doubles as a live demo of the
agent-memory layer ([docs/DESIGN.md](docs/DESIGN.md)).

Fall back to reading files raw only where glance doesn't help: source code,
non-Markdown files, or when you genuinely need a whole file verbatim.

## What this project is

**glance** — a terminal Markdown reader/editor for macOS, written in **C**,
that doubles as an **agent-native memory layer** over a Markdown vault. Two
faces over the same files:

- **User-side** — a rendered TUI with three modes: Reader (rendered preview
  with a block cursor), Insert (full-screen raw editor), Split (editor + live
  preview); plus vault navigation (`[[wikilinks]]`, backlinks, a graph
  explorer, a fuzzy switcher), themes, search, and HTML/PDF export.
- **Agent-side** — token-cheap bounded reads, budget-aware retrieval with a
  token receipt, an MCP server (`glance mcp`), and a surgical write API, so an
  agent reads and maintains a vault for a fraction of the tokens.

The renderer is owned end-to-end: md4c parses Markdown into a structured `Doc`
(lines of styled runs), which every sink consumes — an ANSI serializer, a
notcurses cell blitter, an HTML emitter, and the agent layer's bounded
projections. No glamour, no black box.

> The repository was originally a Go program; that implementation now lives
> only in git history, at the `go-final` tag (`git checkout go-final`). All
> work happens in `src/`.

## Build, test, run

```sh
make                 # build ./glance (TUI) and ./glance-render (CLI)
make test            # every unit suite under UBSan (+ ASan where it can start)
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

- `make test` probes whether an AddressSanitizer binary can start and falls
  back to UBSan alone when it can't — asan's runtime deadlocks at init on
  macOS 26 (the story: [`memory/lessons.md`](memory/lessons.md)). CI
  (macos-latest) runs the same recipe.
- The TUI needs a real terminal, so verify TUI changes by hand. Everything
  pure has a unit test; **new behaviour ships with one**, and `make test`
  stays green and sanitizer-clean.

> **Gotcha:** never run `./glance …` as a background command or with `&` — a
> backgrounded glance spins at 100% CPU (no controlling tty). Run agent CLIs
> synchronously; each finishes in well under a second.

## Architecture

```
src/
  render.h/.c    md4c -> structured Doc (lines of styled runs); headings/links tagged
  doc_ansi.c     Doc -> ANSI string (render CLI + tests)
  doc_html.c     Markdown -> self-contained themed HTML (md4c SAX sink; reuses highlight.c)
  export.c       export a file to HTML, or PDF via a detected external converter
  preprocess.c   tolerant-Markdown fix-ups before parse (bold tighten, setext)
  search.c       case-insensitive full-text search over a Doc
  toc.c          table of contents from the tagged heading lines
  editor.c       rune-aware line-array text buffer
  completion.c   bracket auto-pairing (no backtick/fence pairing)
  fuzzy.c        subsequence fuzzy match + ranking (the Ctrl-P file switcher)
  legend.c       Reader key-sidebar layout (width split, aligned rows)
  progress.c     Reader scroll/progress HUD logic (percent, ride-along, spinner)
  theme.c        color themes: built-in palettes, chrome derivation, config parser
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
tests/           one unit test per pure module (make test)
testdata/        sample.md showcase + an example vault/
```

## Invariants (do not break)

- **One model, many sinks.** The renderer emits a structured `Doc`;
  `doc_ansi.c`, `tui.c`, `doc_html.c`, and the agent projections
  (`section.c`/`context.c`) consume it. Add rendering features in `render.c`,
  never in a sink, so the CLI, TUI, exports, agent reads, and tests stay
  consistent.
- **MCP tools reuse the exact CLI exports.** `mcp.c` runs the same `agent.c`
  functions and captures their stdout, so the CLI and MCP surface never
  drift — change behaviour in `agent.c`, never in two places.
- **The untrusted-input boundary is the MCP server.** `json.c` caps parse
  depth and `mcp.c` validates UTF-8 on output; keep new MCP input paths
  defensive (the audit that shaped this: `docs/archive/REVIEW.md`).
- **The vault is a folder.** No `--init`, no index file. `vault.c` finds the
  root by walking up to a `.git`/`.obsidian` marker and scans it recursively;
  bare `[[names]]` resolve by stem across the whole tree.
- **Cursor sync is offset-based and exact.** `render.c` records each visual
  line's source line during the parse (`src_line_at` → `Line.source_line`);
  `preprocess_map` recovers the original line across inserted blanks.
  Consecutive source lines md4c merges into one paragraph (soft break) share
  the first line's number — the inherent limit of a *rendered* preview, not a
  fixable bug.
- **The file watcher watches the parent directory**, not the file — atomic
  saves use `rename`, which would break an inode-level watch.
- **The TUI owns SIGWINCH.** `NCOPTION_NO_WINCH_SIGHANDLER` + a self-pipe
  polled beside input; on wake it calls `notcurses_refresh` and reflows once.
  Don't hand resize back to notcurses — its `NCKEY_RESIZE` never reaches an
  external poll loop (see `memory/lessons.md`).
- **Legacy keyboard mode is intentional.** `tui.c` disables the kitty
  keyboard protocol at init (`term_kbd_reset`); this fixes an escape-sequence
  leak on exit in some terminals. Don't re-enable enhanced keyboard modes
  without testing exit.
- **`atomic_write` is never called with an empty path** (stdin input has no
  path; the UI prevents saving in that case).

## Conventions (non-negotiable)

- **Documentation style.** Every function is preceded by a `/* ... */` comment
  in **English** stating what it does. Inline comments only when strictly
  necessary; the code should read cleanly on its own.
- **vi-style keys:** `i` = Insert, `e` = Split, `Esc` = Reader. Do not swap.
- **No backtick / code-fence auto-pairing.** Brackets `[ ( {` auto-close;
  fences are typed by hand.
- **Commits credit the author only** — never add a `Co-Authored-By` or any
  assistant trailer in this repo.

## Memory protocol

The living state lives in `memory/` — a glance vault of small notes
(`status`, `decisions`, `lessons`, `history`) behind the index
`memory/MEMORY.md`. It replaced the old STATUS.md / context.md pair on
2026-07-01.

- **Before starting:** read the index, or ask the vault directly —
  `./glance --context "your question" memory/ --budget 2000`.
- **After any non-trivial change** (new behaviour, bug fix, refactor, new or
  retired files): update `memory/status.md` in the same change; add a dated
  entry to `memory/decisions.md` when you settled something a future session
  could second-guess; add to `memory/lessons.md` only what was empirically
  surprising. Agent-layer design shifts also update `docs/DESIGN.md`
  (milestones §9, open questions §11).
- **Distill, don't append.** Rewrite stale lines, merge duplicates, delete
  the superseded — git history is the archive. Keep each note under ~150
  lines, with absolute dates.
- Skip the update only for typo-level edits.

## Where everything else lives

- [`README.md`](README.md) — the product front page: install, usage, both
  faces, every key binding.
- [`memory/`](memory/MEMORY.md) — living state: status · decisions · lessons.
- [`docs/DESIGN.md`](docs/DESIGN.md) — the agent-memory north-star, milestones,
  open questions.
- [`docs/MCP.md`](docs/MCP.md) — wiring the MCP server + the tool reference.
- [`docs/specs/`](docs/specs/) — dated design specs for shipped user-side
  features (legend sidebar, themes, picker, scroll HUD, plugin).
- [`docs/archive/`](docs/archive/) — completed one-time reports: the M1–M4
  handoff and the adversarial review. Historical; their findings are folded
  into the code and this file.
- `commands/`, `skills/`, `.claude-plugin/` — the Claude Code plugin (wraps
  the *installed* `glance`; `make install` first).
