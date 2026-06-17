# glance

The terminal Markdown reader/editor for macOS — read, search, edit, and
*navigate* a folder of `.md` files without leaving your shell.

```
$ glance README.md
```

glance opens any Markdown file in a beautifully rendered reader — with
**syntax-highlighted** code blocks, **column-aligned tables**, and **inline
images**. Press `i` to edit, `e` for a split editor with **live preview**, `/` to
search, `t` for a table of contents, `Enter` to follow a `[[wikilink]]`, and
`Ctrl-G` to walk the link graph of the whole vault — all in the terminal.

It is also a **lens for agents**: `glance --outline`, `--links`, and `--graph`
print a document's structure as JSON, so a tool or an LLM can understand how a
set of notes connects with no server, no embeddings, and no index step.

## Why C, why own the renderer

glance began as a Go program built on the Charmbracelet stack (bubbletea +
glamour). Most of that code existed to *fight* glamour's opacity: inserting
marker paragraphs to survive rendering, stripping ANSI to search and re-injecting
it to highlight. So glance was rewritten in C, owning the whole pipeline:

```
Markdown ──▶ md4c (CommonMark/GFM parser) ──▶ our Doc ──▶ ┌─ ANSI string  (CLI, tests)
             our renderer builds a            (styled     └─ notcurses cells (TUI)
             structured document model         runs)
```

Because *we* build the document model, search reads run text directly, the cursor
is a real cell, the TOC is just the tagged heading lines, and link-following is a
lookup — nothing about the output is a black box. The result is smaller, faster,
and fully under our control.

> glance was originally a Go program built on glamour; that implementation now
> lives only in git history, at the **`go-final`** tag
> (`git checkout go-final`).

## Install

Requires a C11 compiler and two libraries — [md4c](https://github.com/mity/md4c)
(Markdown parser) and [notcurses](https://github.com/dankamongmen/notcurses)
(TUI runtime). `pkg-config` is needed at build time to locate them:

```sh
brew install md4c notcurses pkg-config
git clone https://github.com/LucaTamSapienza/glance
cd glance
make                 # builds ./glance (TUI) and ./glance-render (CLI)
make test            # runs the unit tests under AddressSanitizer/UBSan
make install         # copies both onto your PATH so `glance file.md` works anywhere
```

`make install` puts the binaries in `/usr/local/bin` by default; override with
`make install PREFIX=~/.local` (use a `PREFIX` whose `bin` is on your `PATH`).
`make uninstall` removes them.

## Usage

```sh
glance file.md                 # open in the reader TUI
glance-render -w 80 file.md    # render to ANSI on stdout (-l for a light theme)
cat note.md | glance           # read from stdin
glance --keys                  # diagnostic: print raw key events
```

### Keys

**Reader**

| Key | Action | Key | Action |
|-----|--------|-----|--------|
| `h j k l` / arrows | move cursor | `i` | insert mode (full-screen editor) |
| `g` / `G` | top / bottom | `e` | split: editor + live preview |
| `Ctrl-D` / `Ctrl-U` | half page | `v` / `V` | visual-line select |
| `/` | search | `y` | yank selection → system clipboard |
| `n` / `N` | next / prev match | `t` | table of contents |
| `Enter` | follow link / `[[wikilink]]` | `b` | backlinks panel |
| `-` / `Ctrl-O` | back to previous file | `Ctrl-G` | graph explorer |
| `?` | key legend (sidebar) | `Ctrl-S` | save |
| `:w` `:wq` `:x` `:q` `:q!` | write / quit (vi-style) | | |

Press `?` to slide out a **key legend** on the right: a rounded panel listing the
reader's bindings. It doesn't cover the text — the document reflows into the
space beside it — and it shows its own `Esc · ? close` hint. Press `?` again or
`Esc` to dismiss it. (On a window too narrow to reflow, `?` shows a centered
overlay instead.)

**Trackpad / mouse-wheel scrolling** works in the reader (the cursor rides along
so it stays put on screen). A thin reading-progress readout sits in the top-right
corner — a percentage with a small dots-ring spinner (`◜◠◝◞◡◟`) that comes alive
while you scroll and settles to `◌` when you stop.

### Themes

glance ships several color themes — `auto` (the default: follows your terminal's
dark/light background), `dracula`, `nord`, `gruvbox-dark`, `solarized-dark`,
`solarized-light`, `github-light`. List them, pick one with the flag, or press
**`T`** in the reader to open a **live picker** — move with `j`/`k` and the page
recolors as you browse, `Enter` keeps the choice (and saves it as your default),
`Esc` reverts:

```sh
glance --list-themes            # print the available theme names
glance --theme dracula file.md
glance-render --theme nord file.md
```

Set a default — and define or override your own — in `~/.config/glance/config`:

```ini
theme = nord

[theme:mine]
base = dracula
heading1 = #ff00aa
syntax_string = #a6e22e
```

Themes recolor the document (headings, code, links, syntax) and the UI chrome
(status bar, panels, selection, progress); they ride on your terminal's own
background rather than painting a full-screen color.

**Insert / Split** — type to edit, `Esc` returns to the reader, `Ctrl-S` saves,
`Ctrl-V` pastes an image from the clipboard (saves it in a `<name>_media/` folder
beside the document and inserts a Markdown reference). Brackets `[` `(` `{` auto-close (backticks do not
— you type fences by hand).

In the **graph explorer** (`Ctrl-G`) the current note sits in the centre, notes
that link *to* it on the left and notes it links *to* on the right. `j`/`k` or
↑/↓ select a neighbour, `h`/`l` or ←/→ switch column, `Enter` opens it, `Space`
re-centres the graph on it to walk the vault, `Esc` closes.

## The vault — no init needed

There is no `glance --init` and no index file: **the folder is the vault**, just
like Obsidian. When you open a file, glance finds the vault root by walking up to
the nearest `.git` or `.obsidian` marker (falling back to the file's own
directory), then scans it **recursively** — so `[[wikilinks]]` resolve to notes
anywhere in the tree, including subfolders.

## Agent-facing exports

glance serves two readers at once: the human at the terminal and the agent
reading the same files. The non-interactive subcommands print JSON to stdout:

```sh
glance --outline file.md   # heading tree:    [{level, title, line}, ...]
glance --links   file.md   # outbound links:  [{target, wiki}, ...]
glance --graph   ./notes   # whole-vault link graph: {nodes, edges}
```

An agent can call `glance --graph .` to learn how a document set connects, then
`glance --outline x.md` to navigate one file. See [AGENT_FEATURES.md](AGENT_FEATURES.md)
for the design rationale, and [AGENTS.md](AGENTS.md) if you are running this repo
with a coding agent.

## Use with Claude Code (plugin)

This repo is also a **Claude Code plugin**. Install it from the marketplace:

```sh
claude plugin marketplace add LucaTamSapienza/glance
claude plugin install glance
```

It gives Claude two things (it shells out to your installed `glance`, so run
`make install` first):

- **Slash commands** — `/glance-outline`, `/glance-links`, `/glance-graph`, and
  `/glance-preview` (renders a doc inline in your session).
- **Two skills** — one lets Claude *use* glance to navigate a vault's structure;
  the other has Claude *proactively offer* to render markdown with glance when it
  writes a doc or you ask to read one.

## Repository layout

```
src/            the C application (renderer + TUI + agent exports)
tests/          unit tests, one per pure module (make test)
testdata/       sample.md showcase + a small example vault/
Makefile        build (glance + glance-render) and test targets
STATUS.md       module map, feature list, and design notes
AGENT_FEATURES.md   why the vault/graph/JSON features exist
AGENTS.md       guide for using this repo with a coding agent
```

See [STATUS.md](STATUS.md) for the full module-by-module map.

## License

MIT — part of the glance project by Luca Tamburrano.
