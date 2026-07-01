# glance

A terminal Markdown tool with two faces, over the same folder of `.md` files:

- **For you** — a beautifully rendered reader and a real editor: syntax-highlighted
  code, aligned tables, inline images, vault navigation (`[[wikilinks]]`,
  backlinks, a graph explorer), themes, search. Obsidian, in your shell.
- **For your agent** — an **agent-native memory layer**: token-cheap, structure-aware
  reads, budget-bounded retrieval with a *token receipt*, an **MCP server**, and a
  **surgical write API**. Your assistant reads and maintains your vault for a
  fraction of the tokens — locally, with citations, no embeddings required.

```
$ glance README.md                      # open the reader (you)
$ glance --context "how do we deploy?" ./vault --budget 4000   # retrieve (your agent)
$ glance mcp                            # serve the vault to Claude Desktop / Cursor
```

Same files, same renderer, two consumers. The vault's own structure — headings
and the link graph — is the engine; nothing is a black box, nothing leaves your
machine.

---

## User-side: the reader/editor

```sh
glance file.md                 # open in the reader TUI
glance new.md                  # a path that doesn't exist opens empty; :w creates it
glance-render -w 80 file.md    # render to ANSI on stdout (-l for a light theme)
cat note.md | glance           # read from stdin
glance --help                  # full usage and every key binding
```

glance renders Markdown with **syntax-highlighted** code blocks, **column-aligned
tables**, and **inline images** (pixel graphics where the terminal supports them).
Three modes: **Reader** (rendered, with a block cursor), **Insert** (full-screen
editor), **Split** (editor + live preview).

### Keys

**Reader**

| Key | Action | Key | Action |
|-----|--------|-----|--------|
| `h j k l` / arrows | move cursor | `i` | insert mode (full-screen editor) |
| `g` / `G` | top / bottom | `e` | split: editor + live preview |
| `Ctrl-D` / `Ctrl-U` | half page | `v` / `V` | select chars / lines |
| `/` `n` `N` | search, next, prev | `y` | yank selection → system clipboard |
| `Enter` | follow link / `[[wikilink]]` | `t` | table of contents |
| `-` / `Ctrl-O` | back to previous file | `b` | backlinks panel |
| `?` | key legend (sidebar) | `Ctrl-G` | graph explorer |
| `Ctrl-P` | fuzzy file switcher | `T` | theme picker (live) |
| `Ctrl-S` | save | `:w` `:wq` `:x` `:q` `:q!` | write / quit (vi-style) |

**Fuzzy file switcher** (`Ctrl-P`): type to filter every file in the vault by a
subsequence match (ranked best-first), `↑`/`↓` or `Ctrl-N`/`Ctrl-P` to move,
`Enter` to open, `Esc` to close.

**Live reload across sessions**: if the file changes on disk and your buffer has
no unsaved edits, glance adopts the new version automatically — so a second
glance session editing the same file syncs through. With unsaved edits it instead
shows a conflict prompt: `r` to reload (take the disk copy) or `k` to keep yours.

Press `?` to slide out a **key legend** on the right; the document reflows into
the space beside it. **Trackpad / mouse-wheel scrolling** works (the cursor rides
along), with a small reading-progress readout in the top-right corner.

**Insert / Split** — type to edit, `Esc` returns to the reader, `Ctrl-S` saves,
`Ctrl-V` pastes a clipboard image (saved in a `<name>_media/` folder beside the
document). Brackets `[ ( {` auto-close; you type fences by hand. The editor
soft-wraps long lines to the pane width.

In the **graph explorer** (`Ctrl-G`) the current note sits in the centre, notes
that link *to* it on the left and notes it links *to* on the right; `Space`
re-centres to walk the vault.

### Themes

Twelve built-ins — `auto` (default; follows your terminal background), `dracula`,
`nord`, `gruvbox-dark`, `solarized-dark`, `solarized-light`, `github-light`,
`tokyo-night`, `catppuccin-mocha`, `rose-pine`, `everforest`. Pick
one with `--theme`, list them with `--list-themes`, or press **`T`** in the reader
for a **live picker** (the page recolors as you browse; `Enter` keeps and saves
it, `Esc` reverts). Set a default and define custom palettes in
`~/.config/glance/config`:

```ini
theme = nord

[theme:mine]
base = dracula
heading1 = #ff00aa
```

### The vault — no init needed

**The folder is the vault**, just like Obsidian. glance finds the root by walking
up to the nearest `.git` or `.obsidian` marker (falling back to the file's
directory), then scans it **recursively** — so `[[wikilinks]]` resolve to notes
anywhere in the tree. No `--init`, no index file.

### Export to HTML / PDF

glance owns the renderer, so it can emit a **self-contained, themed HTML page**
(semantic and reflowable, with the same syntax-highlighted code as the terminal —
no JavaScript, no CDN):

```sh
glance-render --html notes.md > notes.html          # HTML to stdout
glance --export notes.md                            # writes notes.html
glance --export notes.md out.pdf                    # PDF (OUT ending in .pdf)
```

PDF is produced by handing the HTML to the first converter found —
**weasyprint**, **wkhtmltopdf**, or headless **Chrome/Chromium**; if none is
available glance writes the `.html` instead and tells you. `--theme` applies to
the export too.

---

## Agent-side: the memory layer

Markdown is the lingua franca agents use for memory and context. The problem:
today an agent reads a vault by loading whole files — expensive, and blind to how
notes connect. glance is the layer in between. Every read is **bounded** so it
stays token-cheap, and the retrieval and write paths are built *for* an agent.

All commands print JSON to stdout; the retrieval ones carry a **token receipt**
(how many tokens they used versus a naive whole-file read).

### Reads

```sh
glance --context "QUERY" DIR [--budget N] [--semantic]   # the headline
glance --section "FILE#Heading"        # one heading's subtree + a token receipt
glance --outline FILE [--depth N] [--abstract]           # heading tree, bounded
glance --neighbors "Note" DIR [--depth H]                # link-graph neighbourhood
glance --backlinks "Note" DIR [--context]                # who links here (+ the line)
glance --since TS DIR                  # notes changed after a Unix timestamp
glance --links FILE                    # a file's outbound links
glance --graph DIR                     # the whole vault's link graph
```

`--context` is the wedge. It assembles the **optimal bundle under a token budget**:
note sections ranked by BM25 fused with a **link-graph prior**, chosen with
diversity and a coarse-to-fine projection (full section, or — if it won't fit —
its abstract), plus a **truncation manifest** so the agent knows what was left out
and can follow up. It returns `{query, budget_tokens, chunks, truncated, receipt}`
— and on a real vault routinely saves 90%+ of the tokens a raw read would cost.

Retrieval is **lexical + graph by default** (local, deterministic, private);
`--semantic` fuses in an embedding score so notes a keyword search would miss can
surface (a dependency-free embedder ships today; a MiniLM-class encoder is a
drop-in behind the same interface).

### Writes — surgical, never a whole-file rewrite

The agent declares **intent + location**; glance does the surgery and writes
atomically (temp file + rename), preserving all other formatting:

```sh
glance --edit FILE append|insert|replace "Heading" "text"
glance --set-frontmatter FILE KEY VALUE
```

It resolves the section by heading (text or slug), ignores headings inside fenced
code, and keeps the vault coherent — so an agent can *maintain* your notes, not
just read them.

### MCP server — native in any agent

```sh
glance mcp
```

`glance mcp` speaks JSON-RPC 2.0 over stdio and exposes the reads and writes as
native [MCP](https://modelcontextprotocol.io) tools (`vault_context`,
`vault_section`, `vault_outline`, `vault_neighbors`, `vault_backlinks`,
`vault_since`, `vault_links`, `vault_graph`, `vault_edit`, `vault_set_frontmatter`).
Wire it into Claude Desktop / Cursor / the Agent SDK in three lines — see
[docs/MCP.md](docs/MCP.md):

```json
{ "mcpServers": { "glance": { "command": "glance", "args": ["mcp"] } } }
```

The design and rationale for the whole memory layer is in
[docs/DESIGN.md](docs/DESIGN.md).

---

## Why C, why own the renderer

glance began as a Go program built on the Charmbracelet stack (bubbletea +
glamour). Most of that code existed to *fight* glamour's opacity. So glance was
rewritten in C, owning the whole pipeline:

```
Markdown ──▶ md4c (CommonMark/GFM) ──▶ our Doc ──▶ ┌─ ANSI string  (CLI, tests)
             our renderer builds a     (styled    ├─ notcurses cells (TUI)
             structured document model  runs)      └─ projections    (agent reads)
```

Because *we* build the document model, search reads run text directly, the TOC is
the tagged heading lines, and the agent layer can project the same note at any
granularity (full / section / abstract) — a glamour black box could not. Local,
fast, and fully under our control.

> The original Go implementation lives only in git history, at the **`go-final`**
> tag (`git checkout go-final`).

## Install

Requires a C11 compiler, [md4c](https://github.com/mity/md4c),
[notcurses](https://github.com/dankamongmen/notcurses), and `pkg-config`:

```sh
brew install md4c notcurses pkg-config
git clone https://github.com/LucaTamSapienza/glance
cd glance
make                 # builds ./glance (TUI) and ./glance-render (CLI)
make test            # unit tests (UBSan + AddressSanitizer where it can start)
make install         # copy onto your PATH (/usr/local/bin; honours PREFIX/DESTDIR)
```

For a no-sudo install: `make install PREFIX=~/.local` (with `~/.local/bin` on your
`PATH`). `make uninstall` removes them.

## Use with Claude Code (plugin)

This repo is also a **Claude Code plugin** (it shells out to your installed
`glance`, so `make install` first):

```sh
claude plugin marketplace add LucaTamSapienza/glance
claude plugin install glance
```

It adds slash commands wrapping the JSON exports and the renderer, plus skills
that let Claude *use* glance to navigate and render a vault.

## Repository layout

```
src/            the C app: renderer + TUI (user-side) + the agent-memory layer
tests/          unit tests, one per pure module (make test)
testdata/       sample.md showcase + a small example vault/
memory/         the repo's own agent-curated memory (status · decisions · lessons · history)
docs/           DESIGN.md (agent-memory north-star) · MCP.md · specs/ · archive/
.claude-plugin/ commands/ skills/   the Claude Code plugin
AGENTS.md       how to work in this repo — humans and agents (CLAUDE.md imports it)
```

## Roadmap / known limitations

User-side: sharper inline images (persistent image planes), `Option`+arrow
word-jump under the legacy keyboard protocol, remote-image fetch, wide-table
wrapping. Agent-side: the real MiniLM semantic tier (persistent `.glance/`
embedding cache + graph-expansion retrieval) is complete on the
`feat/semantic-minilm` branch, pending merge. The living list is
[memory/status.md](memory/status.md).

## License

MIT — part of the glance project by Luca Tamburrano.
