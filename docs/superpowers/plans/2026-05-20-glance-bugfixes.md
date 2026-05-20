# glance Bug Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix four reported bugs in glance so it behaves more like Notion/Obsidian: adjacent ` ``` ` completion, preserved newlines, `:q`-only quit, and readable code blocks.

**Architecture:** Four small, independent fixes across the `app` and `render` packages. Each is verified with a focused test (where automatable) plus a final manual smoke test. No refactoring beyond the four fixes.

**Tech Stack:** Go, Bubbletea (v0.25.0), Bubbles textarea (v0.18.0), Glamour (v0.7.0).

**Spec:** `docs/superpowers/specs/2026-05-20-glance-bugfixes-design.md`

---

## Prerequisites

The working directory is **not** currently a git repository. This plan includes a
commit step per task (frequent commits). Before starting, either:

- Initialize git so the commit steps work:
  ```bash
  git init
  git add -A
  git commit -m "Initial commit: glance before bug-fix batch"
  ```
- Or skip every "Commit" step if you prefer not to use git.

All commands below assume the working directory is the repo root:
`/Users/lucatam/Documents/OpenSourceProject/md_reader`.

---

## File Structure

| File | Change | Responsibility |
|------|--------|----------------|
| `internal/app/completion.go` | Modify | Bug #1 — ` ``` ` pair completion |
| `internal/app/completion_test.go` | Create | Bug #1 — completion test |
| `internal/render/glamour.go` | Modify | Bug #2 + #4 — renderer config |
| `internal/render/glamour_test.go` | Create | Bug #2 + #4 — render tests |
| `internal/app/keys.go` | Modify | Bug #3 — remove `q` quit binding |
| `internal/app/update.go` | Modify | Bug #3 — remove `q` quit case |
| `internal/app/view.go` | Modify | Bug #3 — fix help text |
| `internal/app/app_test.go` | Modify | Bug #3 — quit-behavior tests |

---

## Task 1: Bug #1 — ` ``` ` completion places closing fence adjacent

**Files:**
- Modify: `internal/app/completion.go:104-118` (the `case '`':` branch in `handlePairCompletion`)
- Test: `internal/app/completion_test.go` (create)

- [ ] **Step 1: Write the failing test**

Create `internal/app/completion_test.go`:

```go
package app

import (
	"testing"

	tea "github.com/charmbracelet/bubbletea"
)

// typeRune sends a single rune keypress through the model's Update loop.
func typeRune(m Model, r rune) Model {
	m2, _ := m.Update(tea.KeyMsg{Type: tea.KeyRunes, Runes: []rune{r}})
	return m2.(Model)
}

func TestFenceCompletion(t *testing.T) {
	m := New("", []byte(""), ModeEdit)
	m.width = 80
	m.height = 24
	m.layout()

	// Type three backticks on an empty line.
	m = typeRune(m, '`')
	m = typeRune(m, '`')
	m = typeRune(m, '`')

	if got := m.editor.Value(); got != "``````" {
		t.Fatalf("after typing ```, want %q, got %q", "``````", got)
	}

	// The cursor must sit between the two triples: a typed char lands in the middle.
	m = typeRune(m, 'x')
	if got := m.editor.Value(); got != "```x```" {
		t.Errorf("after typing x, want %q, got %q", "```x```", got)
	}
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `go test ./internal/app/ -run TestFenceCompletion -v`
Expected: FAIL — the current code inserts `` `\n\n``` ``, so the editor value is
`` "```\n\n```" ``, not `` "``````" ``.

- [ ] **Step 3: Apply the fix**

In `internal/app/completion.go`, replace the entire `case '`':` branch:

Replace this:

```go
	case '`':
		// Third backtick on an otherwise-empty line → fenced code block.
		// The line already contains `` from the previous two keystrokes, so
		// we only insert the missing third ` plus the closing fence.
		stripped := strings.TrimLeft(prefix, " \t")
		if stripped == "``" {
			m.editor.InsertString("`\n\n```")
			// Cursor is at end of closing fence; go up twice to land
			// at the end of the opening fence line (ready to type language).
			m.editor.CursorUp()
			m.editor.CursorUp()
			m.dirty = true
			return true
		}
	}
```

With this:

```go
	case '`':
		// Third backtick on an otherwise-empty line → insert the closing
		// fence adjacent to the opening one (``````), with the cursor
		// between the two triples. The user presses Enter to split it.
		stripped := strings.TrimLeft(prefix, " \t")
		if stripped == "``" {
			m.editor.InsertString("````")
			m.editor.SetCursor(col + 1)
			m.dirty = true
			return true
		}
	}
```

Note: `col` is already defined earlier in `handlePairCompletion`
(`col := m.editor.LineInfo().CharOffset`) and is in scope. `InsertString` and
`SetCursor` are already used by the `[` case in the same function.

- [ ] **Step 4: Run the test to verify it passes**

Run: `go test ./internal/app/ -run TestFenceCompletion -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add internal/app/completion.go internal/app/completion_test.go
git commit -m "fix: place closing code fence adjacent on ``` completion"
```

---

## Task 2: Bug #2 — preserve every newline in the preview

**Files:**
- Modify: `internal/render/glamour.go` (the `glamour.NewTermRenderer(...)` call in `newRenderer`)
- Test: `internal/render/glamour_test.go` (create)

- [ ] **Step 1: Write the failing test**

Create `internal/render/glamour_test.go`:

```go
package render

import (
	"regexp"
	"strings"
	"testing"
)

// ansiRE strips SGR escape sequences so tests can assert on plain text.
var ansiRE = regexp.MustCompile("\x1b\\[[0-9;]*m")

func TestPreservedNewlines(t *testing.T) {
	g, err := NewGlamour(80)
	if err != nil {
		t.Fatalf("NewGlamour: %v", err)
	}
	out, err := g.Render("first line\nsecond line\n")
	if err != nil {
		t.Fatalf("Render: %v", err)
	}
	plain := ansiRE.ReplaceAllString(out, "")
	if strings.Contains(plain, "first line second line") {
		t.Errorf("newlines collapsed into a space; want them preserved:\n%q", plain)
	}
	if !strings.Contains(plain, "first line") || !strings.Contains(plain, "second line") {
		t.Errorf("rendered output missing content:\n%q", plain)
	}
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `go test ./internal/render/ -run TestPreservedNewlines -v`
Expected: FAIL — without `WithPreservedNewLines`, glamour collapses the single
newline into a space, so the output contains `"first line second line"`.

- [ ] **Step 3: Apply the fix**

In `internal/render/glamour.go`, in `newRenderer`, add `glamour.WithPreservedNewLines()`
to the renderer options.

Replace this:

```go
	return glamour.NewTermRenderer(
		glamour.WithStyles(style),
		glamour.WithWordWrap(width),
		glamour.WithEmoji(),
	)
```

With this:

```go
	return glamour.NewTermRenderer(
		glamour.WithStyles(style),
		glamour.WithWordWrap(width),
		glamour.WithEmoji(),
		glamour.WithPreservedNewLines(),
	)
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `go test ./internal/render/ -run TestPreservedNewlines -v`
Expected: PASS

- [ ] **Step 5: Run the existing blank-line tests to confirm no regression**

Run: `go test ./internal/app/ -run 'TestExpandBlankLines|TestCountTrailingBlankLines|TestCountLeadingBlankLines' -v`
Expected: PASS — the `GLANCEBLANK` blank-line preprocessing is independent of the
renderer option and must still pass.

- [ ] **Step 6: Commit**

```bash
git add internal/render/glamour.go internal/render/glamour_test.go
git commit -m "fix: preserve source newlines in rendered preview"
```

---

## Task 3: Bug #3 — `q` no longer quits; only `:q` quits

**Files:**
- Modify: `internal/app/update.go:331-332` (remove the quit `case`)
- Modify: `internal/app/keys.go:6` and `internal/app/keys.go:28` (remove the `Quit` binding)
- Modify: `internal/app/view.go:182` (fix help text)
- Test: `internal/app/app_test.go` (append two tests)

- [ ] **Step 1: Write the failing test**

Append these two functions to `internal/app/app_test.go` (the file already imports
`strings`, `testing`, and `tea` — no new imports needed):

```go
func TestQDoesNotQuit(t *testing.T) {
	m := New("", []byte("# Title\n\nbody"), ModeReader)
	m.width = 80
	m.height = 24
	m.layout()

	_, cmd := m.Update(tea.KeyMsg{Type: tea.KeyRunes, Runes: []rune("q")})
	if cmd != nil {
		if _, ok := cmd().(tea.QuitMsg); ok {
			t.Error("pressing q in reader mode should not quit")
		}
	}
}

func TestColonQQuits(t *testing.T) {
	m := New("", []byte("content"), ModeReader)
	m.width = 80
	m.height = 24
	m.layout()

	_, cmd := m.runCommand("q")
	if cmd == nil {
		t.Fatal("expected :q to return a quit command")
	}
	if _, ok := cmd().(tea.QuitMsg); !ok {
		t.Errorf("expected :q to produce tea.QuitMsg, got %T", cmd())
	}
}
```

`TestQDoesNotQuit` is the failing test. `TestColonQQuits` is a regression guard —
it already passes today and must keep passing.

- [ ] **Step 2: Run the test to verify it fails**

Run: `go test ./internal/app/ -run TestQDoesNotQuit -v`
Expected: FAIL — today `q` is bound to quit, so the returned command produces a
`tea.QuitMsg`.

- [ ] **Step 3a: Remove the quit `case` in `update.go`**

In `internal/app/update.go`, inside the `// Mode switches.` `switch` block, remove
the `Quit` case.

Replace this:

```go
	case keyMatch(m.keys.ReaderMode, msg) && m.mode == ModeSplit:
		m.pendingSyncLine = m.editor.Line()
		m.pendingSyncCol = m.editor.LineInfo().ColumnOffset
		m.mode = ModeReader
		m.editor.Blur()
		m.source = m.editor.Value()
		m.tocItems = ExtractTOC(m.source)
		m.layout()
		m.previewGen++
		return m, m.renderCmd(m.previewGen)
	case keyMatch(m.keys.Quit, msg):
		return m, tea.Quit
	case msg.String() == "R" && m.externalChange:
		return m.reloadFromDisk()
	}
```

With this:

```go
	case keyMatch(m.keys.ReaderMode, msg) && m.mode == ModeSplit:
		m.pendingSyncLine = m.editor.Line()
		m.pendingSyncCol = m.editor.LineInfo().ColumnOffset
		m.mode = ModeReader
		m.editor.Blur()
		m.source = m.editor.Value()
		m.tocItems = ExtractTOC(m.source)
		m.layout()
		m.previewGen++
		return m, m.renderCmd(m.previewGen)
	case msg.String() == "R" && m.externalChange:
		return m.reloadFromDisk()
	}
```

- [ ] **Step 3b: Remove the `Quit` field from `keys.go`**

In `internal/app/keys.go`, remove the `Quit` field from the `KeyMap` struct.

Replace this:

```go
type KeyMap struct {
	Quit       key.Binding
	Help       key.Binding
```

With this:

```go
type KeyMap struct {
	Help       key.Binding
```

And remove the `Quit` initializer from `DefaultKeys`.

Replace this:

```go
	return KeyMap{
		Quit:       key.NewBinding(key.WithKeys("q"), key.WithHelp("q", "quit")),
		Help:       key.NewBinding(key.WithKeys("?"), key.WithHelp("?", "help")),
```

With this:

```go
	return KeyMap{
		Help:       key.NewBinding(key.WithKeys("?"), key.WithHelp("?", "help")),
```

- [ ] **Step 3c: Fix the help text in `view.go`**

In `internal/app/view.go`, update the Reader help line so it no longer advertises
bare `q`.

Replace this:

```go
		"Reader:   j/k arrows scroll · gg/G top/bottom · / search (rendered) · t TOC · ? help · q quit",
```

With this:

```go
		"Reader:   j/k arrows scroll · gg/G top/bottom · / search (rendered) · t TOC · ? help · :q quit",
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `go test ./internal/app/ -run 'TestQDoesNotQuit|TestColonQQuits' -v`
Expected: PASS (both)

Also confirm the package still builds (the `Quit` field removal must not leave a
dangling reference):

Run: `go build ./...`
Expected: no output, exit code 0

- [ ] **Step 5: Commit**

```bash
git add internal/app/update.go internal/app/keys.go internal/app/view.go internal/app/app_test.go
git commit -m "fix: require :q to quit; bare q is now a no-op"
```

---

## Task 4: Bug #4 — code blocks render black-on-white

**Files:**
- Modify: `internal/render/glamour.go` (the `newRenderer` function, code-block style block)
- Test: `internal/render/glamour_test.go` (append one regression test)

Note: this is a visual color fix. It **cannot** be verified by an automated test —
`go test` runs without a TTY, so termenv strips all color from the output. The
test below is a regression guard (code blocks must still render); the actual
black-on-white appearance is verified in Task 5's manual smoke test.

- [ ] **Step 1: Write the regression test**

Append this function to `internal/render/glamour_test.go`:

```go
func TestCodeBlockRenders(t *testing.T) {
	g, err := NewGlamour(80)
	if err != nil {
		t.Fatalf("NewGlamour: %v", err)
	}
	out, err := g.Render("```\ncode here\n```\n")
	if err != nil {
		t.Fatalf("Render: %v", err)
	}
	plain := ansiRE.ReplaceAllString(out, "")
	if !strings.Contains(plain, "code here") {
		t.Errorf("code block content missing from render:\n%q", plain)
	}
}
```

- [ ] **Step 2: Run the test to verify it passes (baseline)**

Run: `go test ./internal/render/ -run TestCodeBlockRenders -v`
Expected: PASS — this is a baseline guard; it passes before and after the fix.
Its purpose is to catch a regression where setting `Chroma = nil` breaks code-block
rendering entirely.

- [ ] **Step 3: Apply the fix**

In `internal/render/glamour.go`, in `newRenderer`, add `style.CodeBlock.Chroma = nil`
immediately after the existing code-block color assignments. This routes code-block
rendering through glamour's fallback path, which honors `CodeBlock.BackgroundColor`
and `CodeBlock.Color` (the Chroma path ignores them).

Replace this:

```go
	style.CodeBlock.BackgroundColor = &codeBG
	style.CodeBlock.Color = &codeFG
```

With this:

```go
	style.CodeBlock.BackgroundColor = &codeBG
	style.CodeBlock.Color = &codeFG
	// Disable Chroma so glamour's fallback path is used — only that path
	// honors CodeBlock.BackgroundColor / Color. Trade-off: no syntax
	// highlighting, but code is uniformly readable (black on white).
	style.CodeBlock.Chroma = nil
```

- [ ] **Step 4: Run the test to verify it still passes**

Run: `go test ./internal/render/ -run TestCodeBlockRenders -v`
Expected: PASS — code blocks still render with their content intact.

- [ ] **Step 5: Commit**

```bash
git add internal/render/glamour.go internal/render/glamour_test.go
git commit -m "fix: render code blocks black-on-white for terminal readability"
```

---

## Task 5: Final verification

**Files:** none — this task only builds, tests, and manually smoke-tests.

- [ ] **Step 1: Vet the whole module**

Run: `go vet ./...`
Expected: no output, exit code 0

- [ ] **Step 2: Run the full test suite**

Run: `go test ./...`
Expected: all packages PASS

- [ ] **Step 3: Build the binary**

Run: `go build -o glance ./cmd/glance`
Expected: no output, exit code 0; a `glance` binary is produced

- [ ] **Step 4: Manual smoke test**

Run: `./glance -edit testdata/sample.md`

Verify each fix:

1. **Bug #1** — In the editor pane, type three backticks on an empty line. The line
   should read `` `````` `` with the cursor between the two triples. Press Enter and
   confirm it splits cleanly into an opening fence, a blank line, and a closing fence.
2. **Bug #2** — Type two short lines separated by a single Enter (e.g. `a` then
   `sdsadasda`). In the preview pane they must appear on two separate lines, not
   joined into `a sdsadasda`.
3. **Bug #3** — In reader mode (press `Esc`), press `q`: nothing happens. Type `:q`
   and press Enter: the app quits. (`Ctrl+C` also still quits.)
4. **Bug #4** — Open a file containing a fenced code block (` ``` Ciao ``` `). The
   code block must show black text on a white background, clearly readable on the
   dark terminal.

- [ ] **Step 5: Commit (only if the build produced a tracked artifact change)**

If `glance` is gitignored or untracked, there is nothing to commit here — the task
is complete. Otherwise:

```bash
git add -A
git commit -m "chore: verified glance bug-fix batch"
```

---

## Self-Review Notes

- **Spec coverage:** Bug #1 → Task 1; Bug #2 → Task 2; Bug #3 → Task 3; Bug #4 →
  Task 4; spec "Testing" section → tests in Tasks 1–4 plus Task 5 manual smoke test.
  All four spec sections are covered.
- **Bug #4 honesty:** the spec asks for a render test; color is not unit-testable
  without a TTY, so Task 4 uses a regression guard and Task 5 covers the visual
  check. This is called out explicitly rather than faking a color assertion.
- **No dangling references:** Task 3 removes the `Quit` field; the only consumer
  (`update.go:331`) is removed in the same task, and Step 4 builds the package to
  confirm.
