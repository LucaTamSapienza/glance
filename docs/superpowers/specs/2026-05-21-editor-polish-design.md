# Design: Editor Polish — Word Jump, Row Alignment, Esc Exit, Backtick, Cursor Round-Trip

Date: 2026-05-21

## Context

glance is a terminal Markdown reader/editor for macOS. The north-star is
WYSIWYG behavior: what you type maps line-for-line to what you see in the
preview, and mode switches preserve cursor position. Five issues reported by
the user move us toward that goal:

1. **Opt+←/→ in the editor inserts the literal letters `b`/`f`** instead of
   jumping by word.
2. **Editor and reader panes disagree by one line.** Source has a blank line
   between a heading and the following paragraph; the rendered output
   collapses it, so all subsequent rows are off by one.
3. **Pressing Enter after typing one or three backticks inserts an extra
   backtick.** Backticks must never auto-duplicate; this is reinforced by an
   existing project preference (see `feedback_no_fence_autocompletion` in
   user memory).
4. **The reader→editor→reader round-trip loses the horizontal cursor
   position.** Vertical position survives; horizontal does not.
5. **Exiting Split mode requires two Esc presses** (first blurs the editor,
   second switches to Reader). The intermediate state is friction.

Each fix is small in isolation. They share a domain (cursor/editor/render
interaction) so they go in one design.

## Section 1 — Opt+←/→ word jump in the editor

### Where
`internal/app/completion.go` (`dispatchEditorKey`),
`internal/app/keys.go`.

### Behavior
In `ModeEdit` and focused `ModeSplit`, intercept `tea.KeyLeft` and
`tea.KeyRight` when `msg.Alt` is true and jump to the next/previous
**whitespace boundary on the current source line only** (no wrapping
to the previous/next line — matches the existing "keep it simple" style
of `editorParagraphUp`/`editorParagraphDown`).

- **Opt+→**: from the cursor, skip the current run of spaces (if on one),
  then skip the current run of non-space chars. Land on the first space
  after that run, or on end-of-line.
- **Opt+←**: mirror image. Land on the start of the previous word, or
  on column 0.

### Cleanup
Remove `"alt+b"` from the reader-mode `Left` binding and `"alt+f"` from the
reader-mode `Right` binding in `keys.go`. Those bindings were a workaround
for the same macOS keystrokes; they confuse the new editor-mode handling
and weren't documented behavior.

### Test
New unit test in `internal/app/completion_test.go`, modeled on
`TestEditorParagraphJump`:

- Source: `"foo bar  baz qux"` (length 16; double space between `bar` and `baz`).
- Cursor at column 0. Opt+→: expect column 3 (the space after "foo").
- Opt+→: expect column 7 (the first space after "bar", after first skipping
  no leading spaces because cursor was on a space and we step past it).
- Opt+→ twice more: column 12 (space after "baz"), then 16 (end of line).
- Opt+← from column 16 four times: 13 (start of "qux"), 9 (start of "baz"),
  4 (start of "bar"), 0.

## Section 2 — Editor/Reader 1:1 row alignment

### Where
`internal/render/glamour.go`, `internal/app/preprocess.go`,
`internal/app/model.go`, `internal/app/cursor.go`.

### Strategy
Post-render alignment driven by a source-line → rendered-row map.

1. After Glamour renders, walk source and rendered lines together and build
   `srcToRendered []int` such that `srcToRendered[i]` is the rendered row
   where source line `i` lives. Matching uses the same text-comparison
   approach as the current `findClosestRenderedLine` — but computed once
   per render rather than per query.
2. **Normalize** the rendered output: insert blank rendered rows wherever
   the map skips, so that after normalization source line `i` is always on
   rendered row `i`. Update `srcToRendered` to reflect the normalized
   positions.
3. Store the normalized rendered string and the (now trivial) identity-ish
   map on the Model alongside `m.rendered`.

### Wrapping
If a source line wraps to N rendered rows on a narrow terminal, it stays
as N rows and subsequent source lines shift down by N-1. The map handles
this: `srcToRendered[i+1] = srcToRendered[i] + N`. The contract is
"source line `i` starts at rendered row `srcToRendered[i]`" — strict 1:1
only holds when no source line wraps.

### Why this over preprocessor tricks
Glamour's blank-line handling depends on the block types on each side
(heading vs paragraph vs list). A preprocessor would need to model
Glamour's collapse rules. Aligning *after* rendering observes the actual
output instead of predicting it.

### Side effect
`findClosestRenderedLine` and `readerLineToSource` become trivial lookups
into the stored map, eliminating a long-standing source of cursor drift.

### Test
New `internal/app/align_test.go`:

- Source matching the user's screenshot:
  `"# Ciao\n## io sono Luca\n\n**tu come ti chiami**\n\nsssss"`.
- Render it at a known width.
- Assert that for every source line index `i`, the rendered row at
  `srcToRendered[i]` contains the expected text (heading text, bold
  text stripped of `**`, blank, etc.).
- Assert source lines 2 and 4 (the blanks) map to blank rendered rows.

## Section 2.5 — Single-Esc exit from Split mode

### Where
`internal/app/update.go`, the `if m.mode == ModeSplit && m.editor.Focused()`
branch.

### Change
Replace the current "blur the editor" handler for `esc`/`ctrl+c` with the
same logic the unfocused-split Esc branch uses today (lines ~328–337):

- Capture `m.pendingSyncLine = m.editor.Line()` and
  `m.pendingSyncCol` (which, per Section 4, becomes a source-col read).
- `m.source = m.editor.Value()`.
- `m.tocItems = ExtractTOC(m.source)`.
- `m.mode = ModeReader`.
- `m.editor.Blur()`.
- `m.layout()`.
- `m.previewGen++`.
- Return `m.renderCmd(m.previewGen)`.

### Leave alone for now
The unfocused-split branch (`m.mode == ModeSplit && !m.editor.Focused()`)
stays in place. With single-Esc going straight to Reader, the unfocused-
split state becomes unreachable through normal flow, but `i`/`a`/`o` still
re-focus from it if it's reachable some other way. The implementation plan
can audit and delete dead state in a follow-up if confirmed unreachable.

### Test
New `TestSplitEscGoesToReader` in `internal/app/sync_test.go`:

- Construct a Model in `ModeSplit` with `m.editor.Focus()`.
- Send `tea.KeyMsg{Type: tea.KeyEsc}`.
- Assert `m.mode == ModeReader`.

## Section 3 — Backtick + Enter duplication

### Where
TBD by debugging — likely `internal/app/completion.go` (`dispatchEditorKey`)
or `internal/app/update.go`.

### Behavior
Typing any number of backticks followed by Enter must produce exactly those
backticks plus a newline. No insertions, ever. Reinforces the existing
`feedback_no_fence_autocompletion` user-memory rule.

### Investigation plan (during implementation)

1. Write a failing test in `completion_test.go`:

   ```go
   func TestBacktickThenEnterNoDuplication(t *testing.T) { ... }
   func TestSingleBacktickThenEnterNoDuplication(t *testing.T) { ... }
   ```

   Type the backtick(s) using `typeRune` (the existing test helper), then
   send `tea.KeyMsg{Type: tea.KeyEnter}`. Assert
   `editor.Value() == "```\n"` (and `"`\n"`).
2. If the test passes, the bug isn't in the model layer — it's how the
   terminal delivers Opt+9 keystrokes (terminal sends `alt+9` *and* a
   backtick rune as two events; some Meta-encoding quirk). In that case,
   add env-var-gated `fmt.Fprintf(os.Stderr, ...)` logging around
   `dispatchEditorKey` to capture the exact `tea.KeyMsg` sequence on the
   user's terminal for Opt+9 + Enter, and fix based on observed events.
3. If the test fails, fix it in the model.

### Defensive fix that helps regardless of root cause
In `dispatchEditorKey`'s `tea.KeyRunes` case, skip insertion when
`msg.Alt` is true. Alt-modified runes should be treated as commands
(word jumps, paragraph jumps, etc.) — never as text. This naturally
fixes both the `alt+b`/`alt+f` → `"b"`/`"f"` insertion bug from
Section 1 and any "extra alt-modified backtick rune" the terminal
emits as part of Opt+9.

## Section 4 — Cursor round-trip column preservation

### Where
`internal/app/model.go`, `internal/app/update.go`, `internal/app/cursor.go`.

### Core idea
Single source-of-truth cursor: `(srcLine, srcCol)` on the Model.
All mode switches read and write that. The reader's visual
`(cursorLine, cursorCol)` and the editor's `(line, charOffset)`
become *derived* views.

### Concrete changes

1. **Model fields** (`internal/app/model.go`): add `srcLine int; srcCol int`.
   These are the authoritative cursor position in source coordinates.
   Initialize to `0, 0` in `New()`.
2. **Reader → Editor** (the `i` and `e` cases in `update.go` ~L312, ~L320):
   replace the call to `readerLineToSource()` with a direct read of
   `m.srcLine`, `m.srcCol`. The reader already keeps these in sync (step 4).
3. **Editor → Reader** (the `esc`/`ctrl+c` cases in `update.go` ~L168,
   plus the new Section 2.5 handler): capture `m.srcLine = m.editor.Line()`
   and `m.srcCol` as the logical character offset on the current source
   line — *not* `LineInfo().ColumnOffset` (that's the visual column inside
   the soft-wrapped row and breaks on wrap). The implementation plan
   picks the cleanest bubbles textarea API for absolute logical col;
   candidates are `LineInfo().RowOffset + CharOffset`, or computing it
   from `editor.Value()` + the textarea's exposed position.
4. **Reader cursor movement** (the `h`/`l`/`left`/`right` cases in
   `update.go` ~L384): mutate `m.srcCol` instead of `m.cursorCol`.
   Re-derive `m.cursorCol` for display using Section 2's `srcToRendered`
   map plus a "rendered col for source char N" helper. For styled
   lines (e.g. source `**bold**` → rendered `bold`), the rendered col
   equals the source col minus the count of styling chars consumed
   before that point on the line.
5. **Vertical movement** (`j`/`k`/`up`/`down` in reader): mutate
   `m.srcLine` and re-derive `m.cursorLine` via `srcToRendered`.

### Migration
Keep `pendingSyncLine`/`pendingSyncCol` and `readerLineToSource` during
the transition. Failing tests will indicate when they can be deleted.
Don't remove them in the same change — keep the refactor incremental.

### Test
New `internal/app/cursor_roundtrip_test.go`:

- Build a Model in `ModeReader` with the source from Section 2's test.
- Move the reader cursor to (line 3, col 5) — line 3 contains
  `**tu come ti chiami**`, col 5 in the rendered line is the char `o`
  in "come" (after the stripped `**`).
- Switch to Edit, then back to Reader.
- Assert `m.cursorLine == 3` and `m.cursorCol == 5`.
- Repeat with the cursor on a plain line (no styling) to verify the
  trivial case.

## Out of scope

- TOC behavior, search behavior, file watcher, image rendering.
- Any change to the Reader mode's existing key bindings beyond removing
  `alt+b`/`alt+f` (Section 1) and the Esc semantics in Split (Section 2.5).
- New colon commands.
- Changing Glamour's style sheets.

## Risk and rollback

- **Section 2** is the highest-risk change — it rewrites the post-render
  pipeline. Rollback: revert the alignment-normalization step but keep
  `srcToRendered` as a read-only map used only for cursor sync. That
  reduces Section 2 to the original "map cursor only" option.
- **Section 4** depends on Section 2's map. Land Section 2 first.
- **Section 3**'s defensive fix (skip `msg.Alt` runes) could theoretically
  block a legitimate alt-modified character input on some keyboard layouts.
  If a user reports this, narrow the filter to known-problematic alt-runes
  rather than reverting the whole guard.

## Order of implementation

1. Section 1 (independent, smallest).
2. Section 2.5 (independent, smallest).
3. Section 3 (independent; investigation may inform Section 1's defensive
   filter).
4. Section 2 (foundation for Section 4).
5. Section 4 (depends on Section 2).
