# CLAUDE.md

This file provides guidance to Claude Code when working with code in this repository.

## What this project is

**glance** — a terminal Markdown reader/editor for macOS. It opens `.md` files (or stdin) in a beautifully rendered TUI, with three modes: Reader (rendered preview), Insert (full-screen raw editor), and Split (editor on left + live preview on right). Built with the Charmbracelet stack.

## Commands

```sh
# Build
go build -o glance ./cmd/glance

# Run tests
go test ./...

# Run a single test
go test ./internal/app/ -run TestFindHits

# Vet
go vet ./...

# Run the binary
./glance testdata/sample.md
./glance -edit testdata/sample.md
```

## Architecture

```
cmd/glance/main.go          CLI entry: flags, stdin/file loading, runs tea.Program
internal/app/
  app.go                    Bubbletea Model: all state, Update, View, layout
  keys.go                   KeyMap (key.Binding wrappers for every action)
  search.go                 findHits, highlightMatches, injectColCursor (all ANSI-aware)
  toc.go                    ExtractTOC: fence-aware heading parser → []TOCItem
  completion.go             Markdown pair completion ([→[], ` ` `→fence block, etc.)
  readfile.go               thin os.ReadFile wrapper
internal/render/
  glamour.go                Glamour wrapper: dark/light detection, width-aware renderer
  images.go                 Terminal detection (Kitty/iTerm2/WezTerm/Ghostty) for v1.1 images
internal/fs/
  save.go                   AtomicWrite: tmp file + rename, preserves original mode
  watcher.go                fsnotify wrapper; watches the *parent directory* (survives atomic renames)
```

## Bubbletea model design

The app follows the Elm architecture. Key patterns to understand:

**Three modes** (`ModeReader`, `ModeEdit`, `ModeSplit`):
- Reader: `viewport.Model` shows rendered ANSI output; `cursorLine`/`cursorCol` track a visual cursor drawn inline
- Edit: `textarea.Model` takes all keys except `Esc` and `Ctrl+S`
- Split: editor on left, viewport preview on right; editor only receives keys when `m.editor.Focused()`

**Async render pipeline with debouncing**: `renderNow()` and `debouncedRender()` both return a `tea.Cmd` that runs in a goroutine and returns `previewReadyMsg`. Stale renders are discarded by comparing `msg.gen` to `m.previewGen`. Only the latest render wins.

**Cursor sync across modes**: Glamour adds blank lines, so rendered line count ≠ source line count. When switching Editor→Reader, `syncCursorMsg` carries the source line, and `Update` maps it proportionally to a rendered line. The reverse (`readerLineToSource`) maps rendered cursor back to source when entering the editor.

**File watcher**: `gfs.Watch` watches the parent *directory* (not the file) because atomic saves use `rename`, which would break an inode-level watch. `waitWatch()` is re-issued after every `externalChangeMsg` so the channel stays live.

**Search**: operates on the fully-rendered ANSI string (`m.rendered`). `findHits` strips ANSI before comparing; `highlightMatches` re-injects yellow-bg ANSI codes and re-applies them after any glamour SGR reset mid-match.

## Key invariants

- `m.source` always holds the last-saved-to-disk content in Reader mode; in Edit/Split it trails the textarea until save confirms.
- The renderer is created/recreated inside render goroutines when `r.Width() != w`; do not cache the renderer on the model across width changes.
- `previewWidth()` accounts for mode, TOC panel (28 cols), and the 2-col cursor prefix in reader mode.
- `AtomicWrite` must not be called with `path == ""` (stdin input has no path; the UI prevents saving in that case).

## Maintaining project status

Whenever you make non-trivial changes (new behavior, bug fixes, refactors, new
files, retired files), update both of these in the same change so a future
session can pick up the work without needing prior conversation context:

- `context.md` — keep the **Current status** section accurate: what's on the
  active branch vs `main`, what's still open, what was just resolved. Update
  the date stamp at the top.
- The user's auto-memory `project_glance.md` — keep the package layout, bug
  log, and known-limitations sections current. Add new files when introduced,
  remove entries that are no longer accurate.

Skip the update only for tiny, fully self-contained edits (a typo, a comment
tweak). For anything bigger — feature work, bug fixes, refactors, new test
files — update the status docs in the same edit.
