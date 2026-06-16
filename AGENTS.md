# AGENTS.md

Guidance for AI coding agents (and the humans driving them) working in this
repository. If you are an agent, read this before making changes — it captures
the conventions and invariants that are not obvious from the code alone.

> glance is itself a tool for agents. Beyond editing the source, you can *use*
> the built binary to understand any Markdown vault: `glance --graph .` for the
> link graph, `glance --outline file.md` for a file's heading tree, and
> `glance --links file.md` for its outbound links — all JSON on stdout, no
> server or index required.

## What this project is

**glance** is a terminal Markdown reader/editor for macOS, written in C. It
parses Markdown with [md4c](https://github.com/mity/md4c) into a structured
document model and renders it two ways: to an ANSI string (the `glance-render`
CLI and the tests) and to [notcurses](https://github.com/dankamongmen/notcurses)
cells (the `glance` TUI). It has three modes — Reader, Insert, and Split — plus a
vault model with wikilinks, backlinks, and a graph explorer.

The source of truth is the C app in [`src/`](src/). The original Go
implementation lives in [`legacy-go/`](legacy-go/), is **deprecated and
unmaintained**, and must not be modified — it exists only as a historical
reference until it is removed.

## Build, test, run

```sh
make            # build ./glance (TUI) and ./glance-render (CLI)
make test       # run every unit test under AddressSanitizer + UBSan
make clean      # remove binaries and build artifacts

./glance testdata/sample.md          # try the renderer interactively
./glance-render -w 80 testdata/sample.md   # render to stdout (-l = light theme)
```

Dependencies are `md4c` and `notcurses` (`brew install md4c notcurses`). The
TUI needs a real terminal, so verify TUI changes by hand; everything testable
without a terminal has a unit test, and **new behaviour must come with one**.
`make test` must stay green and ASan/UBSan-clean.

## Conventions (please follow exactly)

- **Documentation style.** Every function is preceded by a `/* ... */` comment in
  **English** stating what it does. Inline comments are used *only* when strictly
  necessary — the code should read cleanly on its own. Match the surrounding
  style; do not add narration.
- **Keys are vi-style.** `i` enters Insert, `e` enters Split, `Esc` returns to
  Reader. Do not swap these.
- **No backtick / code-fence auto-pairing.** Brackets `[ ( {` auto-close; the
  user types fences by hand. Do not add fence completion.
- **Commits credit the author only.** Do not add `Co-Authored-By` or any
  agent/assistant trailer to commits in this repo.
- **Keep the status docs current.** Non-trivial changes update both
  [`STATUS.md`](STATUS.md) (module map, feature list) and [`context.md`](context.md)
  (current status) in the same change. Skip this only for typo-level edits.

## Architecture in one screen

The renderer (`src/render.c`) turns Markdown into a `Doc`: a list of visual
`Line`s, each a sequence of styled `Run`s. Everything else consumes that model:

```
render.c     md4c -> structured Doc (lines of styled runs); headings/links tagged
doc_ansi.c   Doc -> ANSI string (the render CLI and tests)
preprocess.c tolerant-Markdown fix-ups before parsing (bold tighten, setext)
search.c     case-insensitive full-text search over a Doc
toc.c        table of contents from the tagged heading lines
editor.c     rune-aware line-array text buffer
completion.c bracket auto-pairing
highlight.c  spec-driven per-language code highlighter (token classes)
fs_save.c    atomic write (temp file + rename, preserving mode)
fswatch.c    kqueue watch of the parent directory (survives atomic rename)
clipboard.c  pbcopy + open (system clipboard / opening links)
vault.c      vault scan + wikilink resolution (the folder is the vault)
graph.c      the vault link graph (shared by --graph and the Ctrl-G explorer)
agent.c      --outline / --links / --graph JSON exports
util.c       shared UTF-8 + whole-file helpers
tui.c        notcurses front-end: modes, input, drawing, event loop
main.c       glance entry (TUI)   ·   main_render.c   glance-render entry (CLI)
```

Key invariants worth preserving:

- **One model, two sinks.** Add rendering features in `render.c` (the `Doc`), not
  in the ANSI or cell sink, so the CLI, the TUI, and the tests stay consistent.
- **The vault is a folder.** No init, no index. Root is found by walking up to a
  `.git`/`.obsidian` marker; the scan is recursive.
- **Cursor sync is proportional.** md4c exposes no source byte-offsets, so the
  reader↔editor cursor mapping is approximate by design — not a bug to "fix"
  naively.
- **Legacy keyboard mode is intentional.** The TUI disables the kitty keyboard
  protocol at init; this fixes an escape-sequence leak on exit in some terminals.
  Don't re-enable enhanced keyboard modes without testing the exit path.

## Good first tasks

Open items are tracked in [`STATUS.md`](STATUS.md) and [`context.md`](context.md).
The current known gaps: inline images (notcurses supports sixel/kitty/iterm),
table column alignment, and an exact 1:1 reader↔editor cursor map. (Per-language
syntax highlighting in code blocks is done — see `highlight.c`.)
