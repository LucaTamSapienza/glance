# glance for humans: the reader and editor

The user-side face of glance: a rendered terminal reader, a real editor, a
read-only macOS preview window, and HTML/PDF export, all over the same folder
of Markdown that your agent reads through the memory layer described in the
[README](../README.md).

```sh
glance file.md                 # open in the reader TUI
glance file.md --ui            # open a read-only macOS preview window
glance new.md                  # a path that doesn't exist opens empty; :w creates it
glance-render -w 80 file.md    # render to ANSI on stdout (-l for a light theme)
cat note.md | glance           # piped stdin: render to stdout (like glance-render)
glance --help                  # full usage and every key binding
```

glance renders Markdown with **syntax-highlighted** code blocks, **column-aligned
tables**, and **inline images** (pixel graphics where the terminal supports them).
Three modes: **Reader** (rendered, with a block cursor), **Insert** (full-screen
editor), **Split** (editor + live preview).

## macOS preview

`glance file.md --ui` (or `glance --ui file.md`) opens a separate **Glance**
window and immediately returns the terminal prompt. The window displays the
rendered Markdown, with selectable text, tables, images and highlighted code.
It is read-only: there is no editor or save command, and the source stays untouched.
`Cmd-C` copies selected text; `Cmd-O` opens more Markdown files; `Cmd-W` closes
one window and `Cmd-Q` quits. Each document gets its own window. Reopening an
already-open file brings its window forward and keeps your reading position;
close it and open it again to read external changes. CLI-launched instances quit
after their last window closes.

The preview uses the same HTML renderer as the export, inside macOS AppKit /
WebKit. Local images resolve relative to the Markdown file. Web and email links
open in their default apps; local note navigation is not part of this preview.
The window shows the file as it was opened.
Document scripts, forms and embedded frames cannot run or navigate the preview.
The preview defaults to `auto`, following the macOS light/dark appearance at
opening, independently of the terminal theme. Choose another palette with
`--theme NAME` (including custom themes from the config).
No browser, local server or extra runtime is needed.

After installation, use Finder **Open With > Glance** to open `.md` and
`.markdown` documents. To make Glance the default, select a Markdown file and
choose **Get Info > Open with > Glance > Change All…** (Italian Finder:
**Ottieni informazioni > Apri con > Glance > Modifica tutti…**). If Glance is
missing from the list, choose **Other…**, enable **All Applications**, and use
Cmd-Shift-G to select the installed bundle at `PREFIX/libexec/glance/Glance.app`:
`/usr/local/libexec/glance/Glance.app` by default, or
`~/.local/libexec/glance/Glance.app` for the no-sudo installation described in the [README](../README.md#install). Repeat
for each extension you want to associate. Glance registers as a Markdown viewer;
installing it does not change your default app. Launching Glance directly offers
the standard Open panel; **File > Open…** (Cmd-O) remains available after closing
all windows.

## Keys

**Reader**

| Key | Action | Key | Action |
|-----|--------|-----|--------|
| `h j k l` / arrows | move cursor | `i` | insert mode (full-screen editor) |
| `Alt`/`Ctrl` + `←`/`→` | jump by word | `Cmd`+`←`/`→` (`Ctrl-A`/`E`) | line start / end |
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
the space beside it. **Trackpad / mouse-wheel scrolling** works in every mode
(the cursor rides along), with a small reading-progress readout in the top-right
corner of the reader.

**Insert / Split** — type to edit, `Esc` returns to the reader, `Ctrl-S` saves,
`Ctrl-V` pastes a clipboard image (saved in a `<name>_media/` folder beside the
document). Brackets `[ ( {` auto-close; you type fences by hand. The editor
soft-wraps long lines to the pane width. `Alt`/`Ctrl`+`←`/`→` jump by word and
`Ctrl-A`/`Ctrl-E` go to line start/end; if `Option`/`Cmd`+arrows type letters
instead of moving, your terminal hides those modifiers in legacy mode — set
`keyboard = enhanced` in the config (below) to use the kitty keyboard protocol,
and verify what your terminal sends with `glance --keys`.

In the **graph explorer** (`Ctrl-G`) the current note sits in the centre, notes
that link *to* it on the left and notes it links *to* on the right; `Space`
re-centres to walk the vault.

## Themes

Twelve built-ins — `auto` (default; follows your terminal background), `dracula`,
`nord`, `gruvbox-dark`, `solarized-dark`, `solarized-light`, `github-light`,
`tokyo-night`, `catppuccin-mocha`, `rose-pine`, `everforest`. Pick
one with `--theme`, list them with `--list-themes`, or press **`T`** in the reader
for a **live picker** (the page recolors as you browse; `Enter` keeps and saves
it, `Esc` reverts). Set a default and define custom palettes in
`~/.config/glance/config`:

```ini
theme = nord
# opt in to the kitty keyboard protocol: Option/Cmd + arrow chords carry real
# modifier bits on terminals that support it (default is legacy mode)
keyboard = enhanced

[theme:mine]
base = dracula
heading1 = #ff00aa
```

## The vault — no init needed

**The folder is the vault**, just like Obsidian. glance finds the root by walking
up to the nearest `.git` or `.obsidian` marker (falling back to the file's
directory), then scans it **recursively** — so `[[wikilinks]]` resolve to notes
anywhere in the tree. No `--init`, no index file.

## Export to HTML / PDF

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
