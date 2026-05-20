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
