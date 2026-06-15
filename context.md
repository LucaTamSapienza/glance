# glance — Project Context

> A recap of what this project is, where it's going, and its current status.
> Intended as orientation context for anyone (including Claude) picking up the work.
> Last updated: 2026-06-15 (visual-select copy added).

## What it is

**glance** is a terminal Markdown reader and editor for macOS. It opens `.md`
files (or stdin) in a rendered TUI built on the Charmbracelet stack. It has three
modes:

- **Reader** — the rendered Markdown preview, with a navigable cursor.
- **Insert** — a full-screen raw-text editor.
- **Split** — the raw editor on the left, a live rendered preview on the right.

## Vision

The north-star is to make glance behave like a **WYSIWYG Markdown editor — Notion
or Obsidian — but entirely in the terminal**. Concretely that means:

- What you type in the editor maps line-for-line to what you see in the preview
  (no surprising reflow, blank lines and newlines preserved).
- Navigation, editing, and quitting feel native and predictable.
- Code blocks, headings, and other elements look polished, not raw.

Most of the work on the project is iterative polishing toward that goal:
removing the small frictions that make a terminal Markdown tool feel unlike a
real editor.

## How to build and run

```sh
go build -o glance ./cmd/glance      # build the binary
go test ./...                        # run all tests
go vet ./...                         # vet
./glance testdata/sample.md          # open a file in Reader mode
./glance -edit testdata/sample.md    # open in Split mode (editor + preview)
```

Go is a compiled language (similar in spirit to C, with garbage collection).
`go build` produces the `glance` binary; `go run ./cmd/glance <file>` compiles
and runs in one step without keeping the binary.

## Architecture

The app follows the Elm architecture (Bubbletea): `Update(msg) -> (Model, Cmd)`
and `View(Model) -> string`.

```
cmd/glance/main.go         CLI entry: flags, stdin/file loading, runs the program
internal/app/
  model.go                 Model struct, constants, New(), Init()
  update.go                Update(), handleKey(), runCommand(), saveCmd()
  view.go                  View(), viewReader(), viewSplit(), overlayHelp()
  preview.go                renderCmd(), debouncedRender(), previewWidth(), layout()
  cursor.go                 cursor mapping between source and rendered lines
  preprocess.go             setext/thematic-break + blank-line preprocessing
  completion.go             editor key dispatch, pair completion, paragraph jumps
  keys.go                   KeyMap / DefaultKeys()
  search.go                 ANSI-aware search over the rendered output
  toc.go                    table-of-contents extraction
  clipboard.go              copyToClipboard wrapper (atotto/clipboard → pbcopy)
  helpers.go                small shared helpers
internal/render/
  glamour.go                Glamour renderer config (dark/light, code blocks)
  images.go                 terminal capability detection (for future image support)
internal/fs/
  save.go                   AtomicWrite (tmp file + rename)
  watcher.go                file-watcher (watches the parent directory)
```

Deeper conventions and invariants are documented in `CLAUDE.md`.

Key libraries: Bubbletea (TUI runtime), Bubbles (textarea/viewport widgets),
Glamour (Markdown-to-ANSI rendering), Goldmark (the Markdown parser inside
Glamour).

## Current status (2026-06-14)

Active work lives on the **`bugfixes` branch**, pushed to GitHub at
`git@github.com:LucaTamSapienza/glance.git`. Build is clean
(`go build ./...`) and **all tests pass** (`go test ./...`).

### 2026-06-15 — copy-to-clipboard (vi-style visual line select)

User couldn't copy text out of glance: `tea.WithMouseCellMotion()` (main.go)
enables mouse reporting, so the terminal's native click-drag selection is
disabled, and there was no in-app copy command. Added a vi-style copy path that
writes **source markdown** to the **system clipboard** (so it survives outside
glance, via `pbcopy`/atotto):

- **`v` / `V`** in Reader toggles line-wise visual selection (anchor = current
  `srcLine`, other end follows the cursor as you move with j/k/g/G). A blue
  gutter bar marks selected rendered rows; the status bar shows
  `-- VISUAL -- N line(s)`.
- **`y`** in visual mode copies the selected source-line range and exits.
- **`yy`** in normal Reader copies the current line (pendingY double-key, like
  `gg`). Any other key cancels a pending `y`.
- **`esc`** cancels an active selection.
- New `internal/app/clipboard.go` (`copyToClipboard` → `atotto/clipboard`,
  promoted from indirect to direct dep). `yankLines`/pure `selectionText` in
  `update.go`; `srcLineToRow` helper in `cursor.go`; highlight + status in
  `view.go`; help/usage text updated in `view.go` + `main.go`.
- Tests: `yank_test.go` (`TestSelectionText`). Build/vet/`go test ./...` clean.

Note: native **mouse** click-drag selection is still captured by mouse
reporting — on macOS terminals hold **Option** (Ghostty/iTerm) while dragging
for native select, or use `v`/`yy`. Not changed in code (wheel-scroll relies on
mouse reporting).

### 2026-06-15 — v0.1.0 released + CI/install fixes

- **Module installable.** `go.mod` path is `github.com/LucaTamSapienza/glance`
  (matches the repo). `go install github.com/LucaTamSapienza/glance/cmd/glance@latest`
  and `@v0.1.0` both work. The earlier 404 was the old (mismatched) module path
  plus a stale proxy cache.
- **First tag `v0.1.0`** created and pushed. The `release` workflow built and
  **published the GitHub Release** (darwin/linux × amd64/arm64 tarballs +
  `checksums.txt`) successfully.
- **Release workflow now green-able.** The run had failed *only* at the final
  Homebrew step: `brews:` pointed at a non-existent tap
  `lucatam/homebrew-glance` (wrong owner casing, deprecated `brews:` key). Per
  user decision the `brews:` section was **removed** from `.goreleaser.yaml`;
  Homebrew is deferred. Everything else in the release pipeline is unchanged.
- **`test.yml` trigger fixed**: was `branches: [main]` but the default branch is
  `master`, so push-triggered tests never ran. Now `branches: [master]`.
- **README install section rewritten**: `go install`, pre-built release binaries,
  and build-from-source; the broken Homebrew command is gone.

### 2026-06-14 — Markdown-tolerance work merged to `master`

Two user-requested rendering fixes were added in `internal/app/preprocess.go`
and **merged into `master`** (fast-forward) on this date:

- **Setext headings neutralized.** `text` followed by a bare `-`, `--`, `=` or
  `==` line no longer turns the text above into a heading — it stays a
  paragraph. `preventSetextFromThematicBreaks` now also fires on setext
  underlines (new helper `isSetextUnderline`), not just thematic breaks. Three+
  dashes (`---`) still render as a horizontal rule, as before. glance prefers
  explicit ATX (`#`/`##`) headings.
- **Space-tolerant bold.** `** parola **` and `__ parola __` now render bold:
  `tightenBoldDelimiters` strips the spaces hugging bold delimiters before
  Glamour sees them. Only bold is touched (single-star/underscore italic left
  alone to avoid clashing with bullet lists). Fenced code blocks and inline
  code spans are preserved verbatim. Known trade-off: outside code, `2 ** 3 **
  4` would now bold the `3` (CommonMark would not) — use a code span for math.

Tests: `TestPreventSetextFromThematicBreaks` (extended), `TestIsThematicBreak`,
`TestTightenBoldDelimiters`, `TestTightenBoldDelimitersSkipsFence`.

The diagnostic/Bug-2 changeset (group B, see below) was deliberately **kept out
of this merge** — `master` only received the setext+bold work plus doc updates.

> **Git history was rewritten** (filter-branch) to strip all Claude co-author
> lines — the repo must show only `@LucaTamSapienza`
> (`tutordimatematica.ing@gmail.com`) as author. Because of that rewrite, any
> SHA from before the rewrite (e.g. the old `54edeec`, `72e078d`) is **stale
> and no longer exists**. The current editor-polish commits are
> `af32d4b..aa2838b` (see `git log master..bugfixes`).

### ✅ Bug 2 resolved and merged (2026-06-15)

The cursor reader↔editor sync (Bug 2) is **confirmed fixed by the user** and
**merged into `master`**. The diagnostic scaffolding has been stripped:
`internal/app/debug.go` and `cmd/probe_render/` were deleted, and every
`debugLogf(...)` / `m.debugState(...)` call removed. The committed fix is just
the real logic:

- `update.go` — `previewReadyMsg` now maps `pendingSyncLine` through the
  authoritative `srcToRendered` map (falling back to `findClosestRenderedLine`
  only if out of bounds); Down/Up reader handlers call `clampSrcColToLine`.
- `cursor.go` — new `clampSrcColToLine` (clamps `srcCol` to the current source
  line length; trades away sticky-column memory).
- `completion.go` — `alt+b`/`alt+f` routed to `editorWordLeft`/`Right`.
- Tests: `completion_test.go`, `sync_test.go`.

The tree is otherwise clean. The only untracked files are scratch test fixtures
under `testdata/` (e.g. `bugfixes.md`, `codeblocks.md`, `new.md`, `v1.md`,
`README.md`) — not committed.

### Behaviors delivered on this branch and verified working

From earlier rounds:

- **Newlines preserved**, **`q` no longer quits**, **`Ctrl+C` saves before
  quit**, **code blocks readable**, **no `` ``` `` autocompletion**, **editor
  viewport follows cursor**, **`Opt+↑/↓` jumps by paragraph**.

From the "editor polish" round (specs + plan in `docs/superpowers/`):

- **`Opt+←/→` word-jump in the editor** — ✅ **CONFIRMED FIXED by the user.**
  macOS Italian-layout Ghostty/Terminal delivers these as
  `KeyRunes{runes='b'/'f', Alt=true}` (readline escape sequences), so
  `dispatchEditorKey` intercepts those rune values and routes them to
  `editorWordLeft`/`editorWordRight` before the Alt-rune filter.
- **No phantom `b`/`f` insertion** — Alt-modified runes are dropped by
  `dispatchEditorKey`, never inserted as text. ✅ resolved with the above.
- **Single Esc exits Split → Reader** — no more two-press dance. ✅
- **Backtick + Enter no longer duplicates** — the `` ``` `` autocompletion was
  removed (Luca prefers plain backtick typing; see auto-memory
  `feedback_no_fence_autocompletion`). ✅
- **1:1 editor↔reader row alignment via post-render normalization** — Glamour
  output is padded with blank rows so source line `i` lives at rendered row
  `i`. Driven by `srcToRendered []int` built in `align.go::buildSrcToRendered`
  and applied in `preview.go::renderCmd`. Appears resolved per debug logs; the
  user last reported residual misalignment only in **Split mode**, so treat
  Split alignment as not-fully-confirmed.

### Bug 2 (cursor reader ↔ writer) — RESOLVED (2026-06-15)

The 1-line vertical offset reader→writer is **fixed and user-confirmed**, now
in `master`.

- **Round 1:** vertical motion in reader carried a stale `srcCol` past the EOL
  of a short line → `i` landed the editor at the wrong column. Fixed via
  `cursor.go::clampSrcColToLine` called from the Down/Up reader handlers.
- **Round 2:** the `previewReadyMsg` handler in `update.go` re-derived
  `cursorLine` via the legacy `findClosestRenderedLine` text-heuristic instead
  of the authoritative `srcToRendered` map; on non-distinctive lines it mapped
  to rendered row 0, desyncing `cursorLine` from `srcLine`. Replaced with a
  direct `m.srcToRendered[m.pendingSyncLine]` lookup (heuristic kept only as an
  out-of-bounds fallback).

The diagnostic logging and probe scaffolding used to chase this were stripped
before the merge (see the resolved-and-merged note near the top).

### Open investigation — sh/yaml code-block highlighting

User asked: *"aggiungere `sh` o `yaml` dentro ``` non cambia niente, come
mai?"* A probe (`cmd/probe_render/main.go` over `testdata/codeblocks.md`)
showed **Chroma DOES emit distinct 256-color palettes per language** (go=7
colors, sh=8, yaml=6, plain=3). So highlighting is working; the most likely
cause of "looks the same" is a **terminal palette limited to 8/16 colors**
collapsing the 256-color codes into similar buckets, or a subtle theme. **No
code change made** — awaiting user clarification on what they actually see
(identical to plain, or just less vivid than Go?) before touching the renderer.

### Diagnostic instrumentation — removed (2026-06-15)

The `GLANCE_DEBUGLOG` logger (`internal/app/debug.go`) and the
`cmd/probe_render/` scaffolding have been **deleted**, and all `debugLogf` /
`debugState` calls stripped, now that Bug 2 is confirmed fixed and merged.

## Known limitations & open items
- **Sticky-column memory is lost across vertical moves** — a deliberate
  trade-off in `clampSrcColToLine` for exact reader→editor cursor preservation.
  Most editors keep a "memorized column" so going down through a short line
  and back up to a long line restores the original column. We don't.
- **`gg`/`G`/TOC/mouse/search** navigation paths update `srcLine` via Task 9
  (`renderedLineToSourceLine` in `cursor.go`), but the inverse lookup is a
  linear scan. Fine for current document sizes; revisit if perf matters.
- **`pendingSyncLine`/`pendingSyncCol` and `srcLine`/`srcCol`** are now
  redundant parallel state, both populated at every Editor→Reader transition.
  After Bug 2 settles, the `pendingSync*` path is a candidate for removal
  (the legacy `readerLineToSource` / `findClosestRenderedLine` heuristics
  become dead).
- **`normalizeRendered` wrapping math** is approximate for CJK/wide chars
  (uses `RowOffset*Width + ColumnOffset` which assumes char-wrap, not
  word-wrap).
- **Code-block background** painted behind the text, not as a perfect
  full-width rectangle — Glamour rendering constraint.
- **`Cmd+Arrow` is not possible** — macOS terminals reserve Command and never
  pass it to a terminal app.
- **`internal/app/preprocess_test.go`** is `gofmt`-dirty (pre-existing; left
  untouched).
- **Markdown edge cases:** setext headings (`text` + `-`/`=` underline) are now
  *neutralized* — they stay paragraphs; use `# ATX` for headings. Goldmark
  blank-line collapsing worked around in `preprocess.go`. Space-tolerant bold
  can false-positive on `a ** b ** c` outside code (see 2026-06-14 note above).

## Where to find more

- `CLAUDE.md` — build commands, architecture detail, key invariants.
- `docs/superpowers/specs/` — design spec for the bug-fix work.
- `docs/superpowers/plans/` — the implementation plan that was executed.
- `git log main..bugfixes` — the full, commit-by-commit history of current work.
