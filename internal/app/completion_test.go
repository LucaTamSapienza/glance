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
