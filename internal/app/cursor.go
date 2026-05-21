package app

import "strings"

// readerLineToSource maps the rendered cursor position (cursorLine, cursorCol)
// back to the corresponding source line and column. Used when entering the
// editor from reader mode to preserve the user's position.
func (m *Model) readerLineToSource() (int, int) {
	renderedLines := strings.Split(m.rendered, "\n")
	if m.cursorLine < 0 || m.cursorLine >= len(renderedLines) {
		return 0, 0
	}

	// Find the nearest non-empty rendered line to use as a text anchor.
	targetText := ""
	actualIdx := m.cursorLine
	for d := 0; d < 5; d++ {
		for _, i := range []int{m.cursorLine + d, m.cursorLine - d} {
			if i >= 0 && i < len(renderedLines) {
				text := strings.TrimSpace(ansiRE.ReplaceAllString(renderedLines[i], ""))
				if text != "" {
					targetText = text
					actualIdx = i
					goto foundHook
				}
			}
		}
	}
foundHook:
	sourceLines := strings.Split(m.source, "\n")
	if targetText == "" {
		if m.totalLines <= 0 {
			return 0, 0
		}
		return clamp(m.cursorLine*len(sourceLines)/m.totalLines, 0, len(sourceLines)-1), 0
	}

	// Search the source for the anchor text, starting near the proportional position.
	prop := actualIdx * len(sourceLines) / len(renderedLines)
	for d := 0; d < len(sourceLines); d++ {
		for _, i := range []int{prop + d, prop - d} {
			if i >= 0 && i < len(sourceLines) {
				sLine := strings.TrimSpace(sourceLines[i])
				if sLine != "" && (strings.Contains(sLine, targetText) || strings.Contains(targetText, sLine)) {
					renderedLinePlain := ansiRE.ReplaceAllString(renderedLines[actualIdx], "")
					indent := len(renderedLinePlain) - len(strings.TrimLeft(renderedLinePlain, " "))
					col := m.cursorCol - indent
					if col < 0 {
						col = 0
					}
					return i, col
				}
			}
		}
	}

	return clamp(prop, 0, len(sourceLines)-1), m.cursorCol
}

// findClosestRenderedLine maps a source line index to the nearest rendered
// line index. Used when entering reader mode from the editor.
func (m *Model) findClosestRenderedLine(sourceLineIdx int) int {
	sourceLines := strings.Split(m.source, "\n")
	if sourceLineIdx < 0 || sourceLineIdx >= len(sourceLines) {
		return 0
	}
	targetText := strings.TrimSpace(sourceLines[sourceLineIdx])
	renderedLines := strings.Split(m.rendered, "\n")

	if targetText == "" {
		if len(sourceLines) <= 0 {
			return 0
		}
		return clamp(sourceLineIdx*len(renderedLines)/len(sourceLines), 0, len(renderedLines)-1)
	}

	prop := sourceLineIdx * len(renderedLines) / len(sourceLines)
	for d := 0; d < len(renderedLines); d++ {
		for _, i := range []int{prop + d, prop - d} {
			if i >= 0 && i < len(renderedLines) {
				rLine := strings.TrimSpace(ansiRE.ReplaceAllString(renderedLines[i], ""))
				if rLine != "" && (strings.Contains(rLine, targetText) || strings.Contains(targetText, rLine)) {
					return i
				}
			}
		}
	}
	return clamp(prop, 0, len(renderedLines)-1)
}

// jumpEditorToSourceLine moves the textarea cursor to the given source line
// and column, used when switching from reader to editor mode.
func (m *Model) jumpEditorToSourceLine(line, col int) {
	sourceLines := strings.Split(m.source, "\n")
	line = clamp(line, 0, len(sourceLines)-1)

	m.editor.CursorStart()
	// Move to line 0.  Guard against stall (matches completion.go pattern).
	for m.editor.Line() > 0 {
		before := m.editor.Line()
		m.editor.CursorUp()
		if m.editor.Line() == before {
			break
		}
	}
	// Move down to target line
	for i := 0; i < line; i++ {
		m.editor.CursorDown()
	}
	m.editor.CursorStart()
	m.editor.SetCursor(col)
}
