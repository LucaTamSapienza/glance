# glance — Project Context

> Orientation for anyone (including Claude) picking up the work. The durable
> picture — not a changelog; history lives in git.
> Last updated: 2026-06-19.

## What it is

**glance** is a terminal Markdown tool for macOS, written in **C**, with two faces
over the same folder of `.md` files:

- **User-side** — a rendered reader and a real editor. Three modes: **Reader**
  (rendered preview with a block cursor), **Insert** (full-screen editor), **Split**
  (editor + live preview). Syntax-highlighted code, aligned tables, inline images,
  themes, search, a table of contents, and vault navigation: `[[wikilinks]]`,
  backlinks, and a graph explorer (`Ctrl-G`).
- **Agent-side** — an **agent-native memory layer** over the same vault: token-cheap
  bounded reads, budget-aware retrieval with a token receipt, an **MCP server**, and
  a **surgical write API**. An agent reads and maintains the vault for a fraction of
  the tokens — locally, with citations, no embeddings required.

The renderer is owned end to end: md4c parses Markdown into a structured `Doc`
(visual lines of styled runs), which the sinks consume — an ANSI serializer, a
notcurses cell blitter, and the agent layer's bounded projections.

> The repository began as a Go program; that implementation lives only in git
> history, at the `go-final` tag (`git checkout go-final`). All work is in `src/`.

## Vision

The north-star is a **WYSIWYG Markdown tool — Notion/Obsidian — entirely in the
terminal**, that serves the human at the prompt *and* the agent reading and
writing the same files. The C rewrite exists so glance owns its rendering (md4c →
our `Doc` → ANSI/cells/projections) instead of treating glamour as a black box.
The agent-era direction is specced in [`docs/DESIGN.md`](docs/DESIGN.md); the
milestones there (M1 reads → M2 MCP → M3 semantic → M4 write) are all shipped.

## How to build and run

```sh
make                 # build ./glance (TUI) and ./glance-render (CLI)
make test            # all unit tests (28 suites) under AddressSanitizer + UBSan
make install         # copy both binaries to PREFIX/bin (default /usr/local)

./glance testdata/sample.md                                  # user-side
./glance --context "rendering" testdata/vault --budget 120   # agent-side
./glance mcp                                                 # MCP server
```

Requires `md4c`, `notcurses`, `pkg-config` (`brew install md4c notcurses pkg-config`).

> **Gotcha:** never run `./glance …` as a background command or with `&` — a
> backgrounded glance spins at 100% CPU in some sandboxes (no controlling tty).
> Run agent CLIs synchronously; each finishes in well under a second.

## Current status

Everything below is on `main`, built clean, **27 test suites green** under
ASan/UBSan. (Branch `feat/wysiwyg-inline` adds a 28th suite, `live_test`, for the
in-progress Live mode — see Known gaps.)

### User-side — done
Full reader/editor: three modes; editor soft-wraps long lines; charwise (`v`) and
linewise (`V`) selection with clipboard yank (`y`); search (`/ n N`); TOC (`t`);
atomic save (`:w`/`Ctrl-S`) with kqueue live-reload (clean buffers adopt a
second session's edits in any mode; a dirty buffer raises an `r`/`k` conflict
prompt); open links / follow
`[[wikilinks]]` (Enter) with a back-stack (`-`/`Ctrl-O`); backlinks panel (`b`);
graph explorer (`Ctrl-G`); fuzzy file switcher (`Ctrl-P`, `fuzzy.c`).
Presentation: per-language syntax highlighting
(`highlight.c`), bordered/column-aligned tables, inline images (pixel or
half-block blit, with a `Ctrl-V` clipboard-image paste), heading chips for
`#`/`##`. Twelve color themes (incl. tokyo-night, catppuccin-mocha, rose-pine,
everforest) with a live picker (`T`) and `~/.config/glance/config`.
A hidable key-legend sidebar (`?`), trackpad scrolling with a reading-progress
HUD. Exact offset-based reader↔editor cursor sync.

**Export:** `glance-render --html FILE` emits a self-contained, themed HTML page
(semantic + reflowable, syntax-highlighted code via `highlight.c`, no JS/CDN —
`doc_html.c` is a fourth sink that re-runs md4c rather than projecting the visual
Doc). `glance --export FILE [OUT]` writes HTML, or a PDF when `OUT` ends in
`.pdf` (HTML handed to a detected converter: weasyprint / wkhtmltopdf / headless
Chrome; `export.c`).

### Agent-side — done (the M1–M4 memory layer)
Token-cheap, bounded JSON exports + retrieval + writes + MCP, all reusing the same
`Doc`:
- **Reads:** `--context "Q" DIR [--budget N] [--semantic]` (the budgeted bundle —
  BM25 + a link-graph prior, diversity, coarse-to-fine, truncation manifest, token
  receipt); `--section "FILE#Heading"`; `--outline --depth --abstract`;
  `--neighbors`; `--backlinks --context`; `--since`; plus `--links`/`--graph`.
- **Writes:** `--edit FILE append|insert|replace "Heading" "text"` and
  `--set-frontmatter FILE KEY VALUE` — structure-addressed edits on the raw source
  (formatting preserved, fenced-code headings ignored), written atomically.
- **MCP:** `glance mcp` — stdio JSON-RPC 2.0 exposing all of the above as native
  tools (Claude Desktop / Cursor / SDK). Wiring in [`docs/MCP.md`](docs/MCP.md).
- **Modules:** `section.c`, `receipt.c`, `bm25.c`, `context.c`, `embed.c`,
  `edit.c`, `json.c`, `mcp.c`; orchestrated by `agent.c`.

### Hardening
An adversarial multi-agent review ([`docs/REVIEW.md`](docs/REVIEW.md)) led to a
fix pass on the untrusted-input boundary: a JSON parser depth cap (closed an MCP
stack-overflow DoS), setext-aware `edit_section` (closed a silent REPLACE
data-loss), fenced ```` ``` ````/`~~~` tracking, frontmatter value escaping,
`--edit` op validation, UTF-16 surrogate-pair `\u` decoding, JSON number-grammar
validation, and `emit_id`/`emit_jstr` UTF-8 hardening.

## Known gaps / open items

- **Big bet — inline WYSIWYG rendering (in progress, branch `feat/wysiwyg-inline`):**
  collapse the Reader/Insert split into a single mode that renders markup *in place
  as you type* — write `**ciao**` and it turns bold immediately, like editxr (the
  source line under the cursor stays raw, everything else rendered). This is the
  north-star "Notion/Obsidian in the terminal" UX. **Slice 1 shipped on the branch:**
  a new **Live mode** (`w` from Reader, `MODE_LIVE`) backed by `live.c` — it
  partitions the rendered `Doc` around the active source line (a `LiveView`: kept
  rendered lines + an `active_at` insertion index), so tui.c blits the styled lines
  and draws the active line raw with the editor pane, hardware cursor on it. The
  styled backdrop (`a->preview`) is reparsed as rarely as possible: a structural
  edit (line split/join) reparses immediately; an edit *within* the active line is
  only marked stale (`a->live_stale`) and deferred — that line is drawn raw, so the
  rest stays valid — and the reparse happens when the cursor finally leaves the
  edited line; **pure navigation and within-line typing never reparse**. This
  removed the per-move/per-keystroke md4c reparse that made the first cut janky.
  Next slices: kill the residual layout "breathing" (active line's raw height ≠ its
  rendered height makes lines below shift; fix = cursor-anchored viewport +
  preserving block spacer rows in `live_build`); keep the *block* under the cursor
  raw (lists/tables/code); a default-mode/config toggle; and an incremental
  per-block re-render so even structural edits don't reparse the whole file. A
  config/default-mode toggle, and an incremental re-render to drop the per-line full
  reparse. Note glance's edge over editxr stays the **agent-side token-saving layer**
  (bounded reads, budgeted retrieval, MCP) — the durable differentiator; the WYSIWYG
  mode is about matching the editing feel.
- **Agent-side (DESIGN.md §11):** the semantic tier ships a dependency-free
  feature-hashing embedder behind the `Embedder` interface; a **MiniLM-class
  encoder** is the drop-in upgrade, gated on an on-device latency/heat benchmark to
  run together. A persistent **`.glance/` index cache** and **graph-expansion
  retrieval** (surfacing zero-lexical 1-hop neighbours) are the next two.
- **Review follow-ups (REVIEW.md §3):** low-severity cleanups and clarity nits.
- **User-side residuals:** image decode-per-frame (a persistent-plane cache is the
  right fix), `Option`+arrow word-jump under the legacy keyboard protocol, remote
  (`http`) image fetch, wide-table wrapping, wide-character display widths, and the
  inherent soft-break cursor-sync limit (consecutive lines md4c folds into one
  paragraph share a line number).

## Where to find more

- `README.md` — install, usage, both faces, keys, layout.
- `STATUS.md` — module-by-module map and the full feature list.
- `docs/DESIGN.md` — the agent-memory north-star and roadmap.
- `docs/MCP.md` — wiring the MCP server + the tool reference.
- `docs/HANDOFF.md` — entry point for a review / level-up pass.
- `docs/REVIEW.md` — the adversarial review report.
- `AGENT_FEATURES.md` — why the agent-memory layer exists.
- `AGENTS.md` / `CLAUDE.md` — conventions and invariants for working in the repo.
