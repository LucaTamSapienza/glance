# glance — Project Context

> A recap of what this project is, where it's going, and its current status.
> Intended as orientation context for anyone (including Claude) picking up the work.
> Last updated: 2026-06-14.

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

### ⚠️ Uncommitted work in the tree (READ THIS FIRST on resume)

There is a **252-line staged-but-uncommitted changeset** sitting in the index,
deliberately not committed (user said *"per adesso non committiamo"*). It
contains the diagnostic logging **and** the in-flight round-2 Bug 2 fix:

```
git diff --cached --stat
  internal/app/completion.go       +routing/debug
  internal/app/completion_test.go  +tests
  internal/app/cursor.go           +clamp/lookup
  internal/app/debug.go            NEW (temporary) — debugLogf + Model.debugState
  internal/app/preview.go          +normalizeRendered call / debug
  internal/app/sync_test.go        +round-trip tests
  internal/app/update.go           +srcToRendered lookup + debugState calls
```

Unstaged on top: `CLAUDE.md` (+17, the "Maintaining project status" section)
and `testdata/sample.md` (+5). Untracked: `cmd/probe_render/`, this
`context.md`, and `testdata/{README,codeblocks,new,probe}.md`.

**Decisions still owed to the user:** whether to commit the staged changeset,
and when to strip the diagnostic logging + probe scaffolding before any merge.

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

### Where we left off — Bug 2 (cursor reader ↔ writer) still partial

The user's last word on Bug 2: *"una riga di sbaglio tra reader e writer — se
nel reader sono a riga 1, nel writer sono a riga 2; il cursore sinistra/destra
sembra giusto."* So a **1-line vertical offset reader→writer** persists;
left/right (column) tracking is fine.

- **Round 1:** vertical motion in reader carried a stale `srcCol` past the EOL
  of a short line → `i` landed the editor at the wrong column. Fixed via
  `cursor.go::clampSrcColToLine` called from the Down/Up reader handlers.
- **Round 2 (in the uncommitted staged changeset):** the `previewReadyMsg`
  handler in `update.go` was re-deriving `cursorLine` via the legacy
  `findClosestRenderedLine` text-heuristic instead of the authoritative
  `srcToRendered` map; on non-distinctive lines it mapped to rendered row 0,
  desyncing `cursorLine` from `srcLine`. Replaced the heuristic with a direct
  `m.srcToRendered[m.pendingSyncLine]` lookup.

**Still not resolved.** Next session should reproduce the 1-line offset with a
fresh `GLANCE_DEBUGLOG` run (simple doc passed; a doc with headings/blank-line
collapsing is where it slips), focusing on the source→rendered map around
headings. Do **not** mark Bug 2 done until the user confirms.

### Open investigation — sh/yaml code-block highlighting

User asked: *"aggiungere `sh` o `yaml` dentro ``` non cambia niente, come
mai?"* A probe (`cmd/probe_render/main.go` over `testdata/codeblocks.md`)
showed **Chroma DOES emit distinct 256-color palettes per language** (go=7
colors, sh=8, yaml=6, plain=3). So highlighting is working; the most likely
cause of "looks the same" is a **terminal palette limited to 8/16 colors**
collapsing the 256-color codes into similar buckets, or a subtle theme. **No
code change made** — awaiting user clarification on what they actually see
(identical to plain, or just less vivid than Go?) before touching the renderer.

### Diagnostic instrumentation still in place

`internal/app/debug.go` (in the uncommitted changeset) is a diagnostic logger
gated by `GLANCE_DEBUGLOG=/path`. It logs every `tea.KeyMsg`, dispatch
decisions, mode transitions, the full render pipeline (rawSrc / Glamour out /
normalized / `srcMap`), and `Model.debugState` snapshots at every reader-mode
movement. **Leave it in until Bug 2 is fully resolved**, then strip it in a
clean commit (along with the probe scaffolding) before merging.

## Known limitations & open items

- **Bug 2 round 2** awaiting user smoke-test (see above).
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
