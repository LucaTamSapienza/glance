package app

import (
	"fmt"
	"path/filepath"
	"strings"
	"time"

	"github.com/charmbracelet/lipgloss"
)

// View implements tea.Model.
func (m Model) View() string {
	if m.width == 0 || m.height == 0 {
		return "loading…"
	}

	bodyH := m.height - statusHeight
	if m.cmdActive || m.searchActive {
		bodyH--
	}
	if bodyH < 3 {
		bodyH = 3
	}

	var body string
	switch m.mode {
	case ModeReader:
		body = m.viewReader(bodyH)
	case ModeEdit:
		body = m.editor.View()
	case ModeSplit:
		body = m.viewSplit(bodyH)
	}

	if m.helpOpen {
		body = overlayHelp(body, m.width, bodyH)
	}

	inputLine := ""
	if m.cmdActive {
		inputLine = m.cmdLine.View()
	} else if m.searchActive {
		inputLine = m.search.View()
	}

	status := m.viewStatus()
	parts := []string{body}
	if inputLine != "" {
		parts = append(parts, inputLine)
	}
	parts = append(parts, status)
	return lipgloss.JoinVertical(lipgloss.Left, parts...)
}

func (m Model) viewReader(bodyH int) string {
	rawLines := strings.Split(m.reader.View(), "\n")
	cursorRow := m.cursorLine - m.reader.YOffset
	hitLines := m.hitLineSet()
	q := m.search.Value()
	for i, line := range rawLines {
		absLine := i + m.reader.YOffset
		if q != "" && hitLines[absLine] {
			line = highlightMatches(line, q)
		}
		if i == cursorRow {
			line = injectColCursor(line, m.cursorCol)
			rawLines[i] = "\x1b[1;36m❯ \x1b[0m" + line
		} else {
			rawLines[i] = "  " + line
		}
	}
	content := strings.Join(rawLines, "\n")
	if !m.tocOpen {
		return content
	}
	toc := m.renderTOC(bodyH)
	divider := lipgloss.NewStyle().
		Foreground(lipgloss.Color("240")).
		Render(strings.TrimSuffix(strings.Repeat("│\n", bodyH), "\n"))
	return lipgloss.JoinHorizontal(lipgloss.Top, toc, divider, content)
}

func (m Model) viewSplit(bodyH int) string {
	editorPane := m.editor.View()
	divider := lipgloss.NewStyle().
		Foreground(lipgloss.Color("240")).
		Render(strings.TrimSuffix(strings.Repeat("│\n", bodyH), "\n"))

	previewLines := strings.Split(m.reader.View(), "\n")
	hitLines := m.hitLineSet()
	q := m.search.Value()
	if q != "" {
		for i, line := range previewLines {
			if hitLines[i+m.reader.YOffset] {
				previewLines[i] = highlightMatches(line, q)
			}
		}
	}
	preview := strings.Join(previewLines, "\n")
	return lipgloss.JoinHorizontal(lipgloss.Top, editorPane, divider, preview)
}

func (m Model) renderTOC(h int) string {
	title := lipgloss.NewStyle().Bold(true).Underline(true).Render("Contents")
	lines := []string{title, ""}
	selectedStyle := lipgloss.NewStyle().Foreground(lipgloss.Color("226")).Bold(true)
	focusedStyle := lipgloss.NewStyle().Background(lipgloss.Color("236"))

	for i, it := range m.tocItems {
		indent := strings.Repeat("  ", it.Level-1)
		t := it.Title
		maxLen := tocWidth - len(indent) - 2
		if maxLen > 0 && len(t) > maxLen {
			t = t[:maxLen-1] + "…"
		}
		line := indent + t
		if i == m.tocSelected {
			line = selectedStyle.Render("> " + line)
		} else {
			line = "  " + line
		}
		lines = append(lines, line)
	}
	for len(lines) < h {
		lines = append(lines, "")
	}
	if len(lines) > h {
		lines = lines[:h]
	}
	res := strings.Join(lines, "\n")
	if m.tocFocused {
		res = focusedStyle.Render(res)
	}
	return lipgloss.NewStyle().
		Width(tocWidth - 1).
		Render(res)
}

func (m Model) viewStatus() string {
	left := "[reader]"
	if m.dirty {
		left += "*"
	}
	switch m.mode {
	case ModeEdit:
		left = "[insert]"
		if m.dirty {
			left += "*"
		}
	case ModeSplit:
		left = "[split]"
		if m.dirty {
			left += "*"
		}
	}
	name := m.path
	if name == "" {
		name = "<stdin>"
	} else {
		name = filepath.Base(name)
	}
	right := name
	mid := ""
	if m.status != "" && time.Now().Before(m.statusExpires) {
		mid = m.status
	}
	if len(m.searchHits) > 0 {
		mid = fmt.Sprintf("%s  %d/%d", mid, m.searchIdx+1, len(m.searchHits))
	}
	return lipgloss.NewStyle().
		Background(lipgloss.Color("236")).
		Foreground(lipgloss.Color("252")).
		Width(m.width).
		Render(fmt.Sprintf(" %s  %s  ·  %s ", left, mid, right))
}

func overlayHelp(body string, w, h int) string {
	help := strings.Join([]string{
		"glance — help",
		"",
		"Reader:   j/k arrows scroll · gg/G top/bottom · / search (rendered) · t TOC · ? help · q quit",
		"          i  → insert mode (edit raw text)   e  → split view (live preview)",
		"Insert:   Esc back to reader · ctrl+s quick save",
		"Commands: :w save · :q quit · :q! force · :wq save+quit  (available in reader/split)",
		"Mouse:    wheel to scroll",
		"",
		"press ? to close",
	}, "\n")
	box := lipgloss.NewStyle().
		Border(lipgloss.RoundedBorder()).
		BorderForeground(lipgloss.Color("63")).
		Padding(1, 2).
		Background(lipgloss.Color("235")).
		Foreground(lipgloss.Color("252")).
		Render(help)
	return lipgloss.Place(w, h, lipgloss.Center, lipgloss.Center, box, lipgloss.WithWhitespaceChars(" "))
}
