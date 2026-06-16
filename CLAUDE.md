# CLAUDE.md

This file provides guidance to Claude Code when working with code in this repository.

## What this project is

**glance** — a terminal Markdown reader/editor for macOS, written in **C**. It
opens `.md` files (or stdin) in a rendered TUI with three modes: Reader (rendered
preview with a block cursor), Insert (full-screen raw editor), and Split (editor
on the left + live preview on the right). It also navigates a *vault* of notes:
`[[wikilinks]]`, backlinks, a graph explorer, and JSON exports for agents.

The renderer is owned end-to-end: md4c parses Markdown into a structured `Doc`
(lines of styled runs), which two sinks consume — an ANSI serializer and a
notcurses cell blitter. No glamour, no black box.

> The repository was originally a Go program; that implementation now lives only
> in git history, at the `go-final` tag (`git checkout go-final`). All work
> happens in `src/`.

## Commands

```sh
make                 # build ./glance (TUI) and ./glance-render (CLI)
make test            # all unit tests under AddressSanitizer + UBSan
make install         # copy both binaries to $(PREFIX)/bin (default /usr/local)
make clean           # remove binaries and build artifacts

./glance --help                         # full usage + every key binding
./glance testdata/sample.md             # open in the TUI
./glance-render -w 80 testdata/sample.md   # render to ANSI on stdout (-l = light)
cat note.md | ./glance                  # read from stdin
./glance --outline file.md              # agent export: heading tree as JSON
./glance --graph ./testdata/vault       # agent export: vault link graph as JSON
```

Requires `md4c` and `notcurses` (`brew install md4c notcurses`).

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
  agent.c        --outline / --links / --graph JSON exports
  util.c         shared UTF-8 + whole-file helpers
  tui.c          notcurses front-end: modes, input, drawing, event loop
  main.c         glance entry (TUI)    ·   main_render.c   glance-render entry (CLI)
tests/           one unit test per pure module (make test)
testdata/        sample.md showcase + an example vault/
```

## Design notes & invariants

- **One model, two sinks.** The renderer emits a structured `Doc`; `doc_ansi.c`
  and `tui.c` consume it. Add rendering features in `render.c`, not in a sink, so
  the CLI, TUI, and tests stay consistent.
- **The vault is a folder.** No `--init`, no index. `vault.c` finds the root by
  walking up to a `.git`/`.obsidian` marker and scans it recursively; bare
  `[[names]]` resolve by stem across the whole tree.
- **Cursor sync is proportional.** md4c exposes no source byte-offsets, so the
  reader↔editor cursor mapping is approximate by design. Exact 1:1 needs a
  source-tracking pass — it is a known gap, not a bug to patch naively.
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
- `STATUS.md` — keep the module map and feature list current.
- The user's auto-memory `project_glance_c_autonomous_plan.md` — keep the plan,
  structure, and known-gaps sections current.

Skip the update only for tiny, fully self-contained edits (a typo, a comment
tweak). For anything bigger, update the status docs in the same edit.
