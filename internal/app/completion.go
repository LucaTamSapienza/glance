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
		return true, true
	case tea.KeySpace:
		m.editor.InsertRune(' ')
		return true, true
	case tea.KeyEnter:
		m.editor.InsertRune('\n')
		return true, true
	case tea.KeyUp:
		m.editor.CursorUp()
		return true, false
	case tea.KeyDown:
		m.editor.CursorDown()
		return true, false
	}
	return false, false
}

// handlePairCompletion intercepts specific key presses in the editor and
// applies Markdown-aware pair completion. Returns true if the key was handled
// (the caller should NOT forward it to the textarea).
//
// Completions:
//   - [        → inserts [] with cursor inside; subsequent ] skips over it
//   - ( after] → inserts () with cursor inside; subsequent ) skips over it
//   - ```       → completes a fenced code block with cursor on the blank middle line
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

	return false
}
