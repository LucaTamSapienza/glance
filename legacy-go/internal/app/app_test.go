package app

import (
	"strings"
	"testing"

	tea "github.com/charmbracelet/bubbletea"
)

func TestModeSwitching(t *testing.T) {
	m := New("", []byte("# Test\n\nContent"), ModeReader)
	m.width = 80
	m.height = 24
	m.layout()

	// Press 'i' to enter Edit mode
	m2, _ := m.Update(tea.KeyMsg{Type: tea.KeyRunes, Runes: []rune("i")})
	m = m2.(Model)
	if m.mode != ModeEdit {
		t.Errorf("expected ModeEdit, got %v", m.mode)
	}

	// Press 'Esc' to return to Reader mode
	m2, _ = m.Update(tea.KeyMsg{Type: tea.KeyEsc})
	m = m2.(Model)
	if m.mode != ModeReader {
		t.Errorf("expected ModeReader, got %v", m.mode)
	}

	// Press 'e' to enter Split mode
	m2, _ = m.Update(tea.KeyMsg{Type: tea.KeyRunes, Runes: []rune("e")})
	m = m2.(Model)
	if m.mode != ModeSplit {
		t.Errorf("expected ModeSplit, got %v", m.mode)
	}
}

func TestCommandSaveAs(t *testing.T) {
	m := New("", []byte("some content"), ModeReader)
	m.width = 80
	m.height = 24
	m.layout()

	// Try :w without path on stdin
	m2, _ := m.runCommand("w")
	m = m2.(Model)
	if !strings.Contains(m.status, "use :w <path>") {
		t.Errorf("expected prompt for path, got status: %s", m.status)
	}

	// Try :w test.md
	_, cmd := m.runCommand("w test.md")
	if cmd == nil {
		t.Fatal("expected saveCmd, got nil")
	}
	// We can't easily execute the command because it writes to disk,
	// but we can check if it returns a saveDoneMsg with the newPath.
	msg := cmd()
	sdm, ok := msg.(saveDoneMsg)
	if !ok {
		t.Fatalf("expected saveDoneMsg, got %T", msg)
	}
	if sdm.newPath != "test.md" {
		t.Errorf("expected newPath test.md, got %s", sdm.newPath)
	}
}

func TestTOCInteraction(t *testing.T) {
	m := New("", []byte("# H1\n\n## H2\n\nContent"), ModeReader)
	m.width = 80
	m.height = 24
	m.rendered = "# H1\n\n## H2\n\nContent"
	m.totalLines = 5
	m.layout()

	// Press 't' to open TOC
	m2, _ := m.Update(tea.KeyMsg{Type: tea.KeyRunes, Runes: []rune("t")})
	m = m2.(Model)
	if !m.tocOpen || !m.tocFocused {
		t.Errorf("expected TOC open and focused, got open=%v, focused=%v", m.tocOpen, m.tocFocused)
	}

	if len(m.tocItems) != 2 {
		t.Fatalf("expected 2 TOC items, got %d", len(m.tocItems))
	}

	// Select second item (H2)
	m2, _ = m.Update(tea.KeyMsg{Type: tea.KeyRunes, Runes: []rune("j")})
	m = m2.(Model)
	if m.tocSelected != 1 {
		t.Errorf("expected tocSelected 1, got %d", m.tocSelected)
	}

	// Press 'Enter' to jump
	m2, _ = m.Update(tea.KeyMsg{Type: tea.KeyEnter})
	m = m2.(Model)
	if m.tocFocused {
		t.Error("expected TOC to lose focus after Enter")
	}
	// H2 is on source line 2 (0-indexed).
	// findClosestRenderedLine should map it to rendered line 2 in this simple case.
	if m.cursorLine != 2 {
		t.Errorf("expected cursorLine 2, got %d", m.cursorLine)
	}
}

func TestDebounceLogic(t *testing.T) {
	m := New("", []byte("content"), ModeSplit)
	m.width = 80
	m.height = 24
	m.layout()

	// Simulate a key press in split mode editor
	m2, cmd := m.Update(tea.KeyMsg{Type: tea.KeyRunes, Runes: []rune("x")})
	m = m2.(Model)
	if cmd == nil {
		t.Fatal("expected debouncedRender command")
	}

	// The command should be a tea.Tick that returns a debounceMsg
	msg := cmd()
	dbm, ok := msg.(debounceMsg)
	if !ok {
		t.Fatalf("expected debounceMsg, got %T", msg)
	}
	if dbm.gen != m.previewGen {
		t.Errorf("expected gen %d, got %d", m.previewGen, dbm.gen)
	}

	// Handling debounceMsg should return a renderCmd
	m2, cmd = m.Update(dbm)
	m = m2.(Model)
	if cmd == nil {
		t.Fatal("expected renderCmd")
	}
}

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

func TestCtrlCQuitsWhenClean(t *testing.T) {
	m := New("", []byte("content"), ModeReader)
	m.width = 80
	m.height = 24
	m.layout()

	_, cmd := m.Update(tea.KeyMsg{Type: tea.KeyCtrlC})
	if cmd == nil {
		t.Fatal("expected ctrl+c to quit when there are no unsaved changes")
	}
	if _, ok := cmd().(tea.QuitMsg); !ok {
		t.Errorf("expected tea.QuitMsg, got %T", cmd())
	}
}

func TestCtrlCWarnsWhenDirtyWithoutPath(t *testing.T) {
	m := New("", []byte("content"), ModeReader)
	m.width = 80
	m.height = 24
	m.layout()
	m.dirty = true // unsaved changes; New("") has no file path to save to

	m2, cmd := m.Update(tea.KeyMsg{Type: tea.KeyCtrlC})
	m = m2.(Model)
	if cmd != nil {
		if _, ok := cmd().(tea.QuitMsg); ok {
			t.Error("ctrl+c must not quit with unsaved changes and no file path")
		}
	}
	if !strings.Contains(m.status, "save") {
		t.Errorf("expected a warning mentioning save, got status %q", m.status)
	}
}
