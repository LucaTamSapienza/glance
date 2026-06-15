# glance-c

A terminal Markdown reader/editor for macOS, written in C — a from-scratch port
of [glance](../) that owns its rendering (md4c → our own ANSI/cell output)
instead of relying on a black-box renderer.

Three modes, vi-style keys, full-text search, a table of contents, atomic save,
live reload, system-clipboard yank, and link opening — all in the terminal.

## Build

Requires `md4c` and `notcurses` (via Homebrew) and a C11 compiler:

```sh
brew install md4c notcurses
make            # builds ./glance (TUI) and ./glance-render (CLI)
make test       # runs the unit tests under AddressSanitizer/UBSan
```

## Use

```sh
./glance README.md                 # open in the TUI
./glance-render -w 80 README.md    # render to ANSI on stdout (-l for light)
cat notes.md | ./glance            # read from stdin
./glance --keys                    # diagnostic: show raw key events
```

### Agent-facing exports (JSON to stdout)

glance is also a lens an agent can call to understand a Markdown vault — no
server, no embeddings, just structure:

```sh
./glance --outline file.md   # heading tree:  [{level,title,line}, ...]
./glance --links file.md     # outbound links: [{target,wiki}, ...]
./glance --graph ./notes     # whole-vault link graph: {nodes, edges}
```

## Keys

**Reader**

| Key | Action | Key | Action |
|-----|--------|-----|--------|
| `h j k l` / arrows | move cursor | `i` | insert mode |
| `g` / `G` | top / bottom | `e` | split (editor + preview) |
| `Ctrl-D` / `Ctrl-U` | half page | `V` | visual-line select |
| `/` | search | `y` | yank selection → clipboard |
| `n` / `N` | next / prev match | `t` | table of contents |
| `Enter` | follow link / `[[wikilink]]` | `?` | help |
| `-` / `Ctrl-O` | back to previous file | `b` | backlinks panel |
| `:w` `:wq` `:q` `:q!` | write / quit | `Ctrl-S` | save |

**Insert / Split:** type to edit, `Esc` returns to Reader, `Ctrl-S` saves.
Brackets `[` `(` `{` auto-close.

## How it works

The renderer turns Markdown into a structured `Doc` — a list of visual lines,
each a sequence of styled runs. Two sinks consume it: an ANSI serializer (for
the CLI and tests) and the notcurses front-end (which writes runs straight to
cells). Because we build the cells, search, the cursor, the TOC, and link
opening all work on the structured model with no escape-string parsing.

See [STATUS.md](STATUS.md) for the module map, feature list, and design notes.

## License / authorship

Part of the glance project by Luca Tamburrano.
