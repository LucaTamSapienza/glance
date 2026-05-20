# Design: glance Bug Fixes (4 bugs)

Date: 2026-05-20

## Context

glance is a terminal Markdown reader/editor. The overarching goal is to make it
behave like Notion/Obsidian, but in the terminal. Four reported bugs block that
goal. This design covers exactly those four — no other scope.

## Bug #1 — ` ``` ` completion places the closing fence on new lines

### Problem

In `internal/app/completion.go`, the backtick case fires when the user types the
third backtick on an otherwise-empty line. It currently inserts `` `\n\n``` ``,
producing a three-line code block and moving the cursor up twice. The user wants
the closing fence placed *adjacent* to the opening one so they can press Enter
themselves.

### Fix

Make ` ``` ` behave like the existing `[` → `[]` pair completion:

- The line already contains `` `` `` (two backticks) before the cursor.
- On the third backtick, insert four backticks with no newlines (the third
  backtick the user is typing, plus the three-backtick closing fence).
- Result: the line reads `` `````` `` (six backticks).
- Place the cursor between the two triples with `SetCursor(col + 1)`, where `col`
  is `m.editor.LineInfo().CharOffset` captured before insertion.

The user then presses Enter to split `` `````` `` into a proper empty code block:

```
```
<cursor>
```
```

The existing guard stays: completion only triggers when the prefix before the
cursor, with leading whitespace stripped, equals `` `` `` (two backticks).

### File

`internal/app/completion.go` — the `case '`':` branch in `handlePairCompletion`.

## Bug #2 — single newlines collapse in the preview

### Problem

Two consecutive non-blank lines in the editor (e.g. `a` then `sdsadasda`) render
as one joined line in the preview (`a sdsadasda`). This is standard CommonMark
behavior: a single newline inside a paragraph is a "soft break" rendered as a
space. Notion/Obsidian instead keep every newline visible, so the editor and the
preview should stay line-for-line aligned.

### Fix

Add `glamour.WithPreservedNewLines()` to the `glamour.NewTermRenderer(...)` call
in `newRenderer` (`internal/render/glamour.go`). This sets glamour's
`ansiOptions.PreserveNewLines = true`, which makes the paragraph reflow keep `\n`
characters instead of replacing them with spaces.

### Notes

- One line of code.
- The existing `GLANCEBLANK` blank-line preprocessing (`expandBlankLines` /
  `restoreExpandedBlanks`) handles blank lines *between* blocks. That is a
  separate concern from soft breaks *within* a paragraph and stays unchanged. A
  test must confirm the two compose correctly.
- Reader-mode cursor sync (`findClosestRenderedLine`, `readerLineToSource`)
  becomes *more* accurate because the rendered line count moves closer to the
  source line count. No regression expected.

### File

`internal/render/glamour.go` — `newRenderer`.

## Bug #3 — pressing `q` quits immediately

### Problem

`internal/app/keys.go` binds `q` to `Quit`, and `internal/app/update.go` acts on
it with `tea.Quit`. The user wants `:q` (already implemented) to be the only way
to quit, so an accidental `q` does not lose work.

### Fix

- Remove the `keyMatch(m.keys.Quit, msg)` quit case from `update.go`.
- After removal, `q` in reader mode falls through to viewport handling, where it
  is not bound — a harmless no-op.
- `:q` continues to work via `runCommand` (handles `q`, `q!`, `wq`, `x`,
  including the dirty-buffer guard).
- `Ctrl+C` stays as a hard-quit escape hatch.
- Update the `Quit` key binding's help text from `q` to `:q` so the help screen
  is accurate.

### Files

`internal/app/keys.go`, `internal/app/update.go`.

## Bug #4 — code blocks are unreadable on a dark terminal

### Problem

Code blocks have no visible background on a dark terminal, so the code is hard to
read. A previous attempt set `style.CodeBlock.BackgroundColor` and
`style.CodeBlock.Color`, but those had no effect.

### Root cause

glamour's code block renderer (`ansi/codeblock.go`) routes rendering through the
Chroma syntax highlighter whenever `CodeBlock.Chroma` is non-nil — which it is in
glamour's `DarkStyleConfig`. On that path, colors come from the Chroma theme, and
`CodeBlock.BackgroundColor` / `CodeBlock.Color` (the `StylePrimitive` fields) are
ignored. Those fields are only honored on glamour's *fallback* path, taken when
`CodeBlock.Chroma` is nil.

### Fix

In `newRenderer` (`internal/render/glamour.go`):

- Set `style.CodeBlock.Chroma = nil` so the renderer takes the fallback path.
- Keep `style.CodeBlock.BackgroundColor` (white, e.g. `#e8e8e8`) and
  `style.CodeBlock.Color` (near-black, e.g. `#1a1a1a`), which the fallback path
  honors.

Result: code blocks render as black text on a white background. No syntax
highlighting — code is monochrome but fully readable. This is the user's chosen
trade-off (simplicity and guaranteed readability over colored highlighting).

### File

`internal/render/glamour.go` — `newRenderer`.

## Testing

- **Unit test** — ` ``` ` completion: after typing the third backtick on an
  empty line, the line equals `` `````` `` and the cursor sits between the
  triples.
- **Unit test** — `q` is a no-op: pressing `q` in reader mode does not produce a
  `tea.Quit` command; `:q` still quits.
- **Render test** — preserved newlines: a paragraph of multiple lines renders
  with the newlines intact, and the existing `GLANCEBLANK` multi-blank-line
  handling still produces the correct blank-line counts.
- **Manual smoke test** — build the binary, open a file, and verify: (1) ` ``` `
  completion places the fence adjacent; (2) multi-line paragraphs stay
  line-for-line aligned in the preview; (3) `q` does nothing, `:q` quits;
  (4) code blocks show black-on-white.

## Out of scope

Only these four bugs. No other refactoring.
