package app

import (
	"testing"

	tea "github.com/charmbracelet/bubbletea"
)

func TestReaderLineToSourceProportionalFallback(t *testing.T) {
	m := Model{
		source:   "line 1\nline 2\nline 3\nline 4\nline 5",
		rendered: "\n\n\n\n\n\n\n\n\n", // 10 empty lines
	}
	m.totalLines = 10

	tests := []struct {
		cursorLine int
		wantSource int
	}{
		{0, 0},
		{2, 1}, // 2 * 5 / 10 = 1
		{4, 2}, // 4 * 5 / 10 = 2
		{9, 4}, // 9 * 5 / 10 = 4.5 -> 4
	}

	for _, tt := range tests {
		m.cursorLine = tt.cursorLine
		got, _ := m.readerLineToSource()
		if got != tt.wantSource {
			t.Errorf("cursorLine %d: want source %d, got %d", tt.cursorLine, tt.wantSource, got)
		}
	}
}

func TestReaderLineToSourceHeuristic(t *testing.T) {
	m := Model{
		source:   "# Header\n\nSome text here\n\nMore text",
		rendered: "\x1b[1mHeader\x1b[0m\n\n\nSome text here\n\n\n\nMore text",
	}
	m.totalLines = 8

	tests := []struct {
		cursorLine int
		wantSource int
	}{
		{0, 0}, // "Header" matches "# Header"
		{3, 2}, // "Some text here" matches "Some text here"
		{7, 4}, // "More text" matches "More text"
	}

	for _, tt := range tests {
		m.cursorLine = tt.cursorLine
		got, _ := m.readerLineToSource()
		if got != tt.wantSource {
			t.Errorf("cursorLine %d: want source %d, got %d", tt.cursorLine, tt.wantSource, got)
		}
	}
}

func TestSplitEscGoesToReader(t *testing.T) {
	m := New("", []byte("# title\n\nbody\n"), ModeSplit)
	m.width = 80
	m.height = 24
	m.layout()
	m.editor.Focus()

	m2, _ := m.Update(tea.KeyMsg{Type: tea.KeyEsc})
	m = m2.(Model)

	if m.mode != ModeReader {
		t.Fatalf("after Esc in focused Split: want mode Reader, got %v", m.mode)
	}
	if m.editor.Focused() {
		t.Fatalf("after Esc in focused Split: editor must be blurred")
	}
}

func TestReaderToEditorUsesSrcCoords(t *testing.T) {
	source := "alpha\nbeta\ngamma\n"
	m := New("", []byte(source), ModeReader)
	m.width = 80
	m.height = 24
	m.layout()
	// Simulate a render result.
	m.rendered = "alpha\nbeta\ngamma\n"
	m.totalLines = 4
	m.srcToRendered = []int{0, 1, 2, 3}

	// srcLine/srcCol point at "gamma" col 3 ('m'), but the rendered-view
	// cursor (cursorLine/cursorCol) is left at line 0 col 0.  If the old
	// readerLineToSource heuristic were still called it would return (0, 0);
	// only the new srcLine/srcCol path produces (2, 3).
	m.srcLine = 2
	m.srcCol = 3
	m.cursorLine = 0
	m.cursorCol = 0

	// Switch to ModeEdit via "i".
	m2, _ := m.Update(tea.KeyMsg{Type: tea.KeyRunes, Runes: []rune{'i'}})
	m = m2.(Model)

	if m.mode != ModeEdit {
		t.Fatalf("want ModeEdit after 'i', got %v", m.mode)
	}
	if got := m.editor.Line(); got != 2 {
		t.Fatalf("editor.Line(): want 2, got %d", got)
	}
	if got := m.editor.LineInfo().ColumnOffset; got != 3 {
		t.Fatalf("editor.LineInfo().ColumnOffset: want 3, got %d", got)
	}
}
