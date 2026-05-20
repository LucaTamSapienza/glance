package app

import (
	"testing"
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
