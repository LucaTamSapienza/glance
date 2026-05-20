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
		m.editor.CursorUp()
		m.syncEditorViewport()
		return true, false
	case tea.KeyDown:
		m.editor.CursorDown()
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
