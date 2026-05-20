package app

import (
	"fmt"
	"strings"
	"testing"

	tea "github.com/charmbracelet/bubbletea"
)

// typeRune sends a single rune keypress through the model's Update loop.
func typeRune(m Model, r rune) Model {
	m2, _ := m.Update(tea.KeyMsg{Type: tea.KeyRunes, Runes: []rune{r}})
	return m2.(Model)
}

func TestBacktickHasNoCompletion(t *testing.T) {
	m := New("", []byte(""), ModeEdit)
	m.width = 80
	m.height = 24
	m.layout()

	// Typing three backticks must produce exactly three backticks — the editor
	// must not auto-insert a closing fence.
	m = typeRune(m, '`')
	m = typeRune(m, '`')
	m = typeRune(m, '`')

	if got := m.editor.Value(); got != "```" {
		t.Fatalf("after typing ```, want %q, got %q", "```", got)
	}
}

func TestEditorViewportFollowsCursor(t *testing.T) {
	var lines []string
	for i := 0; i < 40; i++ {
		lines = append(lines, fmt.Sprintf("line%02d", i))
	}
	m := New("", []byte(strings.Join(lines, "\n")), ModeEdit)
	m.width = 40
	m.height = 8 // editor viewport ends up only a few rows tall
	m.layout()

	// Start at the top of the document.
	for m.editor.Line() > 0 {
		m.editor.CursorUp()
	}

	// Press Down well past the bottom of the visible area.
	for i := 0; i < 30; i++ {
		m2, _ := m.Update(tea.KeyMsg{Type: tea.KeyDown})
		m = m2.(Model)
	}

	view := ansiRE.ReplaceAllString(m.editor.View(), "")
	if !strings.Contains(view, "line30") {
		t.Errorf("viewport did not follow the cursor down — cursor's line not visible.\nview:\n%s", view)
	}
	if strings.Contains(view, "line00") {
		t.Errorf("viewport did not scroll — the top line is still visible after moving the cursor down.\nview:\n%s", view)
	}
}

func TestEditorParagraphJump(t *testing.T) {
	src := "para1 line1\npara1 line2\n\npara2 line1\npara2 line2\n\npara3"
	m := New("", []byte(src), ModeEdit)
	m.width = 40
	m.height = 20
	m.layout()

	// Start at the top of the document.
	for m.editor.Line() > 0 {
		m.editor.CursorUp()
	}

	step := func(msg tea.KeyMsg) {
		m2, _ := m.Update(msg)
		m = m2.(Model)
	}
	optDown := tea.KeyMsg{Type: tea.KeyDown, Alt: true}
	optUp := tea.KeyMsg{Type: tea.KeyUp, Alt: true}

	step(optDown)
	if got := m.editor.Line(); got != 2 {
		t.Fatalf("Opt+Down from line 0: want line 2 (blank), got %d", got)
	}
	step(optDown)
	if got := m.editor.Line(); got != 5 {
		t.Fatalf("Opt+Down from line 2: want line 5 (blank), got %d", got)
	}
	step(optUp)
	if got := m.editor.Line(); got != 2 {
		t.Fatalf("Opt+Up from line 5: want line 2 (blank), got %d", got)
	}
	step(optUp)
	if got := m.editor.Line(); got != 0 {
		t.Fatalf("Opt+Up from line 2: want line 0 (top), got %d", got)
	}
}
