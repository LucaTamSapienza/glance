# glance

The terminal Markdown reader/editor for macOS — read, search, and edit Markdown with live preview, without leaving your shell.

```
$ glance README.md
```

Glance opens any `.md` file in a beautifully rendered reader. Hit `e` to drop into a split editor with **live preview** while you type, `:w` to save, `q` to quit.

## Why

| Tool | Renders | Navigates | **Edits** |
|---|---|---|---|
| Glow | ✓ | partial | ✗ |
| Frogmouth | ✓ | ✓ | ✗ |
| mdcat | ✓ | ✗ | ✗ |
| **glance** | **✓** | **✓** | **✓** |

Markdown is the lingua franca of agent context files (`CLAUDE.md`, `AGENTS.md`), READMEs, specs, and notes. Switching to a GUI editor just to read one is friction worth eliminating.

## Install

**With Go** (requires Go 1.22+):

```sh
go install github.com/LucaTamSapienza/glance/cmd/glance@latest
```

This drops the `glance` binary in `$(go env GOPATH)/bin` — make sure that's on your `PATH`.

**Pre-built binaries** — grab the archive for your OS/arch from the
[latest release](https://github.com/LucaTamSapienza/glance/releases/latest)
(macOS & Linux, amd64 & arm64), then:

```sh
tar -xzf glance_*_darwin_arm64.tar.gz
sudo mv glance /usr/local/bin/
```

**From source:**

```sh
git clone https://github.com/LucaTamSapienza/glance
cd glance
go build -o glance ./cmd/glance
```

> Homebrew tap is planned for a future release.

## Usage

```
glance <file.md>      open a file in the reader
glance -edit file.md  open straight in split editor + preview
cat note.md | glance  read from stdin
```

### Keys

**Reader:**

| Key | Action |
|---|---|
| `j` `k` / arrows | scroll line |
| `gg` / `G` | top / bottom |
| `space` / `pgdn` | page down |
| `/` | incremental search |
| `n` / `N` | next / prev match |
| `t` | toggle Table of Contents |
| `e` | enter split editor + live preview |
| `R` | reload after external file change |
| `?` | help |
| `q` | quit |

**Editor (in split mode):**

| Key | Action |
|---|---|
| `esc` | leave editor, back to reader |
| `:w` | save (atomic write) |
| `:q` | quit (refuses if dirty) |
| `:q!` | force quit |
| `:wq` / `:x` | save + quit |

Mouse is supported in the reader and right preview pane: wheel to scroll, click links.

## How it works

- Rendering: [Glamour](https://github.com/charmbracelet/glamour) — the same renderer Charm's Glow uses
- TUI: [Bubbletea](https://github.com/charmbracelet/bubbletea) + [Bubbles](https://github.com/charmbracelet/bubbles) + [Lipgloss](https://github.com/charmbracelet/lipgloss)
- Live preview: debounced 150 ms after each keystroke
- Save: tmp-file + atomic `rename`, preserving original file mode
- External-change detection: `fsnotify`, prompts `R` to reload

## Roadmap

- v1.1: inline images via Kitty / iTerm2 graphics protocols (terminal detection already shipped)
- v1.2: folder/library view; remote URLs (`glance https://…`)
- v2: Mermaid + math rendering, plugin system

## License

MIT
