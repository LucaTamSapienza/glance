package app

import (
	"strings"

	tea "github.com/charmbracelet/bubbletea"
)

// dispatchEditorKey routes text input and arrow keys to the textarea via
// direct method calls (InsertRune / CursorUp / CursorDown) instead of
// textarea.Update. This sidesteps a bug in bubbles v0.18.0: textarea.Update
// forwards the key to an embedded viewport.Model whose default keymap binds
// j/k/d/u/b/f/space and the arrows to scroll actions, so those keys steal a
// scroll event in addition to doing their text-editing job. The visible
// symptom is that pressing Down (or typing one of those letters in a way that
// also moves the cursor) shifts the textarea's viewport one row, leaving the
// first line invisible. Returns (handled, dirty); when handled the caller
// must NOT also call textarea.Update.
func (m *Model) dispatchEditorKey(msg tea.KeyMsg) (handled, dirty bool) {
	switch msg.Type {
	case tea.KeyRunes:
		if len(msg.Runes) == 0 {
			return false, false
		}
		// Alt-modified runes are commands (word jumps etc.), not text. Some
		// macOS terminals emit a stray Alt-modified rune alongside Opt+arrow
		// or Opt+digit keypresses; inserting them produces phantom characters
		// in the buffer.
		if msg.Alt {
			return true, false
		}
		for _, r := range msg.Runes {
			m.editor.InsertRune(r)
		}
		m.syncEditorViewport()
		return true, true
	case tea.KeySpace:
		m.editor.InsertRune(' ')
		m.syncEditorViewport()
		return true, true
	case tea.KeyEnter:
		m.editor.InsertRune('\n')
		m.syncEditorViewport()
		return true, true
	case tea.KeyUp:
		if msg.Alt {
			m.editorParagraphUp()
		} else {
			m.editor.CursorUp()
		}
		m.syncEditorViewport()
		return true, false
	case tea.KeyDown:
		if msg.Alt {
			m.editorParagraphDown()
		} else {
			m.editor.CursorDown()
		}
		m.syncEditorViewport()
		return true, false
	case tea.KeyLeft:
		if !msg.Alt {
			return false, false
		}
		m.editorWordLeft()
		m.syncEditorViewport()
		return true, false
	case tea.KeyRight:
		if !msg.Alt {
			return false, false
		}
		m.editorWordRight()
		m.syncEditorViewport()
		return true, false
	}
	return false, false
}

// repositionMsg is a no-op message fed to textarea.Update purely to make it
// run its (unexported) viewport repositioning. It is never emitted as a real
// tea.Msg through the program.
type repositionMsg struct{}

// syncEditorViewport scrolls the textarea's internal viewport so the cursor
// stays visible. The direct cursor methods used by dispatchEditorKey
// (InsertRune / CursorUp / CursorDown) move the cursor but never reposition
// the viewport — only textarea.Update does that, via the repositionView()
// it runs at the end of every Update.
//
// View() is called first because the textarea only refreshes its viewport's
// content inside View(); without it, repositionView() would scroll against a
// stale (pre-edit) content height and could leave a just-typed line off
// screen. The subsequent Update with a non-key message then repositions with
// no editing side effect.
func (m *Model) syncEditorViewport() {
	m.editor.View()
	m.editor, _ = m.editor.Update(repositionMsg{})
}

// editorParagraphDown moves the editor cursor down to the next blank line
// (or the last line of the document) — the Opt+Down "jump by paragraph".
func (m *Model) editorParagraphDown() {
	lines := strings.Split(m.editor.Value(), "\n")
	target := len(lines) - 1
	for r := m.editor.Line() + 1; r < len(lines); r++ {
		if strings.TrimSpace(lines[r]) == "" {
			target = r
			break
		}
	}
	for m.editor.Line() < target {
		before := m.editor.Line()
		m.editor.CursorDown()
		if m.editor.Line() == before {
			break
		}
	}
}

// editorParagraphUp moves the editor cursor up to the previous blank line
// (or the first line of the document) — the Opt+Up "jump by paragraph".
func (m *Model) editorParagraphUp() {
	lines := strings.Split(m.editor.Value(), "\n")
	target := 0
	for r := m.editor.Line() - 1; r >= 0; r-- {
		if strings.TrimSpace(lines[r]) == "" {
			target = r
			break
		}
	}
	for m.editor.Line() > target {
		before := m.editor.Line()
		m.editor.CursorUp()
		if m.editor.Line() == before {
			break
		}
	}
}

// editorWordRight moves the editor cursor right by one whitespace-delimited
// word on the current source line. From a space, it first skips the run of
// spaces, then skips the run of non-space chars. From a non-space, it skips
// the current word. Lands on the first space after the word, or end-of-line.
func (m *Model) editorWordRight() {
	lines := strings.Split(m.editor.Value(), "\n")
	row := m.editor.Line()
	if row >= len(lines) {
		return
	}
	runes := []rune(lines[row])
	col := m.editor.LineInfo().CharOffset

	for col < len(runes) && runes[col] == ' ' {
		col++
	}
	for col < len(runes) && runes[col] != ' ' {
		col++
	}
	m.editor.SetCursor(col)
}

// editorWordLeft moves the editor cursor left by one whitespace-delimited
// word on the current source line. Lands on the start of the previous word,
// or column 0.
func (m *Model) editorWordLeft() {
	lines := strings.Split(m.editor.Value(), "\n")
	row := m.editor.Line()
	if row >= len(lines) {
		return
	}
	runes := []rune(lines[row])
	col := m.editor.LineInfo().CharOffset
	if col == 0 {
		return
	}
	col--
	for col > 0 && runes[col] == ' ' {
		col--
	}
	for col > 0 && runes[col-1] != ' ' {
		col--
	}
	m.editor.SetCursor(col)
}

// handlePairCompletion intercepts specific key presses in the editor and
// applies Markdown-aware pair completion. Returns true if the key was handled
// (the caller should NOT forward it to the textarea).
//
// Completions:
//   - [        → inserts [] with cursor inside; subsequent ] skips over it
//   - ( after] → inserts () with cursor inside; subsequent ) skips over it
func (m *Model) handlePairCompletion(msg tea.KeyMsg) bool {
	if msg.Type != tea.KeyRunes || len(msg.Runes) != 1 {
		return false
	}

	lines := strings.Split(m.editor.Value(), "\n")
	row := m.editor.Line()
	if row >= len(lines) {
		return false
	}

	runes := []rune(lines[row])
	col := m.editor.LineInfo().CharOffset

	charAfter := ""
	if col < len(runes) {
		charAfter = string(runes[col])
	}
	prefix := string(runes[:col])

	switch msg.Runes[0] {
	case '[':
		// [] pair: cursor lands inside the brackets
		m.editor.InsertString("[]")
		m.editor.SetCursor(col + 1)
		m.dirty = true
		return true

	case ']':
		// Skip over an auto-inserted ]
		if charAfter == "]" {
			m.editor.SetCursor(col + 1)
			return true
		}

	case '(':
		// After ]: complete the Markdown link with ()
		if strings.HasSuffix(prefix, "]") && charAfter != "(" {
			m.editor.InsertString("()")
			m.editor.SetCursor(col + 1)
			m.dirty = true
			return true
		}

	case ')':
		// Skip over an auto-inserted )
		if charAfter == ")" {
			m.editor.SetCursor(col + 1)
			return true
		}
	}

	return false
}
