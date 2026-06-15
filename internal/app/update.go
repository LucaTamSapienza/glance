package app

import (
	"fmt"
	"strings"

	"github.com/charmbracelet/bubbles/textarea"
	tea "github.com/charmbracelet/bubbletea"

	gfs "github.com/LucaTamSapienza/glance/internal/fs"
)

// Update implements tea.Model.
func (m Model) Update(msg tea.Msg) (tea.Model, tea.Cmd) {
	var cmds []tea.Cmd

	switch msg := msg.(type) {
	case tea.WindowSizeMsg:
		m.width = msg.Width
		m.height = msg.Height
		m.layout()
		cmds = append(cmds, m.renderNow())
		return m, tea.Batch(cmds...)

	case previewReadyMsg:
		if msg.gen != m.previewGen {
			return m, nil
		}
		m.renderer = msg.renderer
		m.rendered = msg.rendered
		m.srcToRendered = msg.srcMap
		m.reader.SetContent(msg.rendered)
		m.totalLines = strings.Count(msg.rendered, "\n") + 1
		if m.cursorLine >= m.totalLines {
			m.cursorLine = m.totalLines - 1
		}
		if m.searchActive || len(m.searchHits) > 0 {
			m.searchHits = findHits(m.rendered, m.search.Value())
		}
		if m.pendingSyncLine >= 0 {
			// Use the authoritative srcToRendered map produced by this render
			// rather than the legacy text-matching heuristic, which mis-mapped
			// source lines with non-distinctive text (e.g. plain paragraphs)
			// to rendered row 0 and silently desynced cursorLine from srcLine.
			if m.pendingSyncLine < len(m.srcToRendered) {
				m.cursorLine = m.srcToRendered[m.pendingSyncLine]
			} else {
				m.cursorLine = m.findClosestRenderedLine(m.pendingSyncLine)
			}
			renderedLines := strings.Split(m.rendered, "\n")
			if m.cursorLine >= 0 && m.cursorLine < len(renderedLines) {
				renderedLinePlain := ansiRE.ReplaceAllString(renderedLines[m.cursorLine], "")
				indent := len(renderedLinePlain) - len(strings.TrimLeft(renderedLinePlain, " "))
				m.cursorCol = m.pendingSyncCol + indent
			} else {
				m.cursorCol = m.pendingSyncCol
			}
			m.pendingSyncLine = -1
			m.pendingSyncCol = 0
			if m.cursorLine < m.reader.YOffset {
				m.reader.SetYOffset(m.cursorLine)
			} else if m.cursorLine >= m.reader.YOffset+m.reader.Height {
				m.reader.SetYOffset(m.cursorLine - m.reader.Height + 1)
			}
		}
		return m, nil

	case debounceMsg:
		if msg.gen == m.previewGen {
			return m, m.renderCmd(msg.gen)
		}
		return m, nil

	case saveDoneMsg:
		if msg.err != nil {
			m.flash("save failed: " + msg.err.Error())
		} else {
			m.dirty = false
			m.flash("written")
			if m.mode == ModeEdit || m.mode == ModeSplit {
				m.source = m.editor.Value()
			}
			if msg.newPath != "" {
				m.path = msg.newPath
				if m.watchCh == nil {
					if ch, _, err := gfs.Watch(m.path); err == nil {
						m.watchCh = ch
						cmds = append(cmds, m.waitWatch())
					}
				}
			}
			m.tocItems = ExtractTOC(m.source)
		}
		return m, tea.Batch(cmds...)

	case externalChangeMsg:
		m.externalChange = true
		m.flash("file changed on disk — press R to reload")
		return m, m.waitWatch()

	case tea.MouseMsg:
		switch m.mode {
		case ModeReader, ModeSplit:
			var c tea.Cmd
			m.reader, c = m.reader.Update(msg)
			if m.mode == ModeReader {
				m.cursorLine = clamp(m.reader.YOffset, 0, m.totalLines-1)
				m.srcLine = m.renderedLineToSourceLine(m.cursorLine)
			}
			cmds = append(cmds, c)
		}
		return m, tea.Batch(cmds...)

	case tea.KeyMsg:
		return m.handleKey(msg)
	}

	return m, tea.Batch(cmds...)
}

func (m Model) handleKey(msg tea.KeyMsg) (tea.Model, tea.Cmd) {
	// Active command line consumes all keys.
	if m.cmdActive {
		switch msg.Type {
		case tea.KeyEsc:
			m.cmdActive = false
			m.cmdLine.SetValue("")
			m.cmdLine.Blur()
			m.layout()
			return m, nil
		case tea.KeyEnter:
			cmd := strings.TrimSpace(m.cmdLine.Value())
			m.cmdActive = false
			m.cmdLine.SetValue("")
			m.cmdLine.Blur()
			m.layout()
			return m.runCommand(cmd)
		}
		var c tea.Cmd
		m.cmdLine, c = m.cmdLine.Update(msg)
		return m, c
	}

	// Active search input consumes all keys.
	if m.searchActive {
		switch msg.Type {
		case tea.KeyEsc:
			m.searchActive = false
			m.search.Blur()
			m.layout()
			return m, nil
		case tea.KeyEnter:
			q := m.search.Value()
			m.searchActive = false
			m.search.Blur()
			m.layout()
			m.searchHits = findHits(m.rendered, q)
			m.searchIdx = 0
			m.jumpToHit()
			return m, nil
		}
		var c tea.Cmd
		m.search, c = m.search.Update(msg)
		return m, c
	}

	// Global save shortcut — works in all modes.
	if msg.String() == "ctrl+s" {
		if m.path == "" {
			m.flash("stdin input — use :w <path> to save to a file")
			return m, nil
		}
		return m, m.saveCmd("")
	}

	// Full-screen editor: all keys go to the textarea except Esc / ctrl+c.
	if m.mode == ModeEdit {
		switch msg.String() {
		case "esc", "ctrl+c":
			m.pendingSyncLine = m.editor.Line()
			m.pendingSyncCol = m.editor.LineInfo().ColumnOffset
			m.srcLine = m.editor.Line()
			m.srcCol = m.editorSourceCol()
			m.source = m.editor.Value()
			m.tocItems = ExtractTOC(m.source)
			m.mode = ModeReader
			m.editor.Blur()
			m.layout()
			m.previewGen++
			return m, m.renderCmd(m.previewGen)
		case "tab":
			m.editor.InsertString("    ")
			m.dirty = true
			return m, nil
		}
		if m.handlePairCompletion(msg) {
			return m, nil
		}
		if handled, dirty := m.dispatchEditorKey(msg); handled {
			if dirty {
				m.dirty = true
			}
			return m, nil
		}
		prev := m.editor.Value()
		var c tea.Cmd
		m.editor, c = m.editor.Update(msg)
		if m.editor.Value() != prev {
			m.dirty = true
		}
		return m, c
	}

	// Split mode with focused editor.
	if m.mode == ModeSplit && m.editor.Focused() {
		switch msg.String() {
		case "esc", "ctrl+c":
			m.pendingSyncLine = m.editor.Line()
			m.pendingSyncCol = m.editor.LineInfo().ColumnOffset
			m.srcLine = m.editor.Line()
			m.srcCol = m.editorSourceCol()
			m.source = m.editor.Value()
			m.tocItems = ExtractTOC(m.source)
			m.mode = ModeReader
			m.editor.Blur()
			m.layout()
			m.previewGen++
			return m, m.renderCmd(m.previewGen)
		case "tab":
			m.editor.InsertString("    ")
			m.dirty = true
			return m, m.debouncedRender()
		}
		if m.handlePairCompletion(msg) {
			m.dirty = true
			return m, m.debouncedRender()
		}
		if handled, dirty := m.dispatchEditorKey(msg); handled {
			if dirty {
				m.dirty = true
				return m, m.debouncedRender()
			}
			return m, nil
		}
		prev := m.editor.Value()
		var c tea.Cmd
		m.editor, c = m.editor.Update(msg)
		if m.editor.Value() != prev {
			m.dirty = true
			return m, tea.Batch(c, m.debouncedRender())
		}
		return m, c
	}

	// Global shortcuts (reader / unfocused split).
	switch {
	case msg.String() == "ctrl+c":
		if m.dirty {
			if m.path == "" {
				m.flash("unsaved changes — :w <path> to save, or :q! to force-quit")
				return m, nil
			}
			return m, tea.Sequence(m.saveCmd(""), tea.Quit)
		}
		return m, tea.Quit
	case keyMatch(m.keys.Help, msg):
		m.helpOpen = !m.helpOpen
		return m, nil
	case keyMatch(m.keys.Command, msg):
		m.cmdActive = true
		m.cmdLine.Focus()
		m.cmdLine.SetValue("")
		m.layout()
		return m, nil
	case keyMatch(m.keys.Search, msg) && (m.mode == ModeReader || (m.mode == ModeSplit && !m.editor.Focused())):
		m.searchActive = true
		m.search.Focus()
		m.search.SetValue("")
		m.layout()
		return m, nil
	case keyMatch(m.keys.NextHit, msg):
		if len(m.searchHits) > 0 {
			m.searchIdx = (m.searchIdx + 1) % len(m.searchHits)
			m.jumpToHit()
		}
		return m, nil
	case keyMatch(m.keys.PrevHit, msg):
		if len(m.searchHits) > 0 {
			m.searchIdx = (m.searchIdx - 1 + len(m.searchHits)) % len(m.searchHits)
			m.jumpToHit()
		}
		return m, nil
	case keyMatch(m.keys.TOC, msg) && m.mode == ModeReader:
		m.tocOpen = !m.tocOpen
		if !m.tocOpen {
			m.tocFocused = false
		} else {
			m.tocFocused = true
			m.tocSelected = 0
		}
		m.layout()
		return m, m.renderNow()
	case msg.String() == "tab" && m.tocOpen && m.mode == ModeReader:
		m.tocFocused = !m.tocFocused
		return m, nil
	}

	// TOC navigation.
	if m.tocFocused && m.tocOpen {
		switch msg.String() {
		case "esc":
			m.tocFocused = false
			return m, nil
		case "j", "down":
			m.tocSelected = clamp(m.tocSelected+1, 0, len(m.tocItems)-1)
			return m, nil
		case "k", "up":
			m.tocSelected = clamp(m.tocSelected-1, 0, len(m.tocItems)-1)
			return m, nil
		case "enter":
			if len(m.tocItems) > 0 {
				item := m.tocItems[m.tocSelected]
				m.srcLine = item.LineNum - 1
				m.srcCol = 0
				m.cursorLine = m.findClosestRenderedLine(m.srcLine)
				m.reader.SetYOffset(m.cursorLine)
				m.tocFocused = false
			}
			return m, nil
		}
		return m, nil
	}

	// Mode switches.
	switch {
	case msg.String() == "i" && m.mode == ModeReader:
		m.mode = ModeEdit
		m.editor.SetValue(m.source)
		m.editor.Focus()
		m.layout()
		m.jumpEditorToSourceLine(m.srcLine, m.srcCol)
		return m, textarea.Blink
	case keyMatch(m.keys.EditMode, msg) && m.mode == ModeReader:
		m.mode = ModeSplit
		m.editor.SetValue(m.source)
		m.editor.Focus()
		m.layout()
		m.jumpEditorToSourceLine(m.srcLine, m.srcCol)
		return m, tea.Batch(textarea.Blink, m.renderNow())
	case keyMatch(m.keys.ReaderMode, msg) && m.mode == ModeSplit:
		m.pendingSyncLine = m.editor.Line()
		m.pendingSyncCol = m.editor.LineInfo().ColumnOffset
		m.srcLine = m.editor.Line()
		m.srcCol = m.editorSourceCol()
		m.mode = ModeReader
		m.editor.Blur()
		m.source = m.editor.Value()
		m.tocItems = ExtractTOC(m.source)
		m.layout()
		m.previewGen++
		return m, m.renderCmd(m.previewGen)
	case msg.String() == "R" && m.externalChange:
		return m.reloadFromDisk()
	}

	// Scrolling / motion in reader and unfocused split.
	if m.mode == ModeReader || (m.mode == ModeSplit && !m.editor.Focused()) {
		// Visual selection + yank (line-wise, vi-style). Reader mode only.
		if m.mode == ModeReader {
			switch msg.String() {
			case "v", "V":
				m.selecting = !m.selecting
				if m.selecting {
					m.selAnchor = m.srcLine
				}
				m.pendingY = false
				return m, nil
			case "y":
				if m.selecting {
					return m.yankLines(m.selAnchor, m.srcLine)
				}
				if m.pendingY {
					m.pendingY = false
					return m.yankLines(m.srcLine, m.srcLine)
				}
				m.pendingY = true
				return m, nil
			case "esc":
				if m.selecting {
					m.selecting = false
					m.pendingY = false
					return m, nil
				}
			}
			// Any other key cancels a pending `yy`.
			m.pendingY = false
		}

		if m.pendingG {
			m.pendingG = false
			if msg.String() == "g" {
				m.cursorLine = 0
				m.cursorCol = 0
				m.srcLine = 0
				m.srcCol = 0
				m.reader.GotoTop()
				return m, nil
			}
		} else if msg.String() == "g" {
			m.pendingG = true
			return m, nil
		}

		switch {
		case keyMatch(m.keys.Top, msg):
			m.cursorLine = 0
			m.cursorCol = 0
			m.srcLine = 0
			m.srcCol = 0
			m.reader.GotoTop()
			return m, nil
		case keyMatch(m.keys.Bottom, msg):
			m.cursorLine = max(0, m.totalLines-1)
			m.cursorCol = 0
			m.srcLine = strings.Count(m.source, "\n")
			m.srcCol = 0
			m.reader.GotoBottom()
			return m, nil
		case keyMatch(m.keys.Down, msg):
			if m.mode == ModeReader {
				sourceLines := strings.Split(m.source, "\n")
				m.srcLine = clamp(m.srcLine+1, 0, len(sourceLines)-1)
				m.clampSrcColToLine(sourceLines)
				if m.srcLine < len(m.srcToRendered) {
					m.cursorLine = m.srcToRendered[m.srcLine]
				} else {
					m.cursorLine = clamp(m.cursorLine+1, 0, m.totalLines-1)
				}
				if m.cursorLine >= m.reader.YOffset+m.reader.Height {
					m.reader.SetYOffset(m.cursorLine - m.reader.Height + 1)
				}
				return m, nil
			}
		case keyMatch(m.keys.Up, msg):
			if m.mode == ModeReader {
				sourceLines := strings.Split(m.source, "\n")
				m.srcLine = clamp(m.srcLine-1, 0, len(sourceLines)-1)
				m.clampSrcColToLine(sourceLines)
				if m.srcLine < len(m.srcToRendered) {
					m.cursorLine = m.srcToRendered[m.srcLine]
				} else {
					m.cursorLine = clamp(m.cursorLine-1, 0, m.totalLines-1)
				}
				if m.cursorLine < m.reader.YOffset {
					m.reader.SetYOffset(m.cursorLine)
				}
				return m, nil
			}
		case keyMatch(m.keys.Left, msg) && m.mode == ModeReader:
			if m.srcCol > 0 {
				m.srcCol--
				m.cursorCol = m.srcCol
			}
			return m, nil
		case keyMatch(m.keys.Right, msg) && m.mode == ModeReader:
			m.srcCol++
			m.cursorCol = m.srcCol
			return m, nil
		}
		prevYOffset := m.reader.YOffset
		var c tea.Cmd
		m.reader, c = m.reader.Update(msg)
		if m.mode == ModeReader && m.reader.YOffset != prevYOffset {
			m.cursorLine = clamp(m.reader.YOffset, 0, m.totalLines-1)
			m.srcLine = m.renderedLineToSourceLine(m.cursorLine)
		}
		return m, c
	}

	// Unfocused split: vi-style keys refocus the editor.
	switch msg.String() {
	case "i", "a", "o":
		m.editor.Focus()
		return m, textarea.Blink
	}
	var c tea.Cmd
	m.reader, c = m.reader.Update(msg)
	return m, c
}

// yankLines copies the inclusive range of source lines [a, b] to the system
// clipboard (markdown source, so it can be pasted outside glance) and clears
// any active visual selection.
func (m Model) yankLines(a, b int) (tea.Model, tea.Cmd) {
	text, n := selectionText(m.source, a, b)

	m.selecting = false
	m.pendingY = false

	if err := copyToClipboard(text); err != nil {
		m.flash("copy failed: " + err.Error())
		return m, nil
	}
	if n == 1 {
		m.flash("copied 1 line")
	} else {
		m.flash(fmt.Sprintf("copied %d lines", n))
	}
	return m, nil
}

// selectionText returns the inclusive range of source lines [a, b] joined by
// newlines, along with the number of lines. Indices are clamped to the source
// and may be given in either order.
func selectionText(source string, a, b int) (string, int) {
	if a > b {
		a, b = b, a
	}
	lines := strings.Split(source, "\n")
	a = clamp(a, 0, len(lines)-1)
	b = clamp(b, 0, len(lines)-1)
	return strings.Join(lines[a:b+1], "\n"), b - a + 1
}

// runCommand executes a colon command entered in the command line.
func (m Model) runCommand(cmd string) (tea.Model, tea.Cmd) {
	cmd = strings.TrimSpace(cmd)
	if cmd == "w" {
		if m.path == "" {
			m.flash("stdin input — use :w <path> to save to a file")
			return m, nil
		}
		return m, m.saveCmd("")
	}
	if strings.HasPrefix(cmd, "w ") {
		newPath := strings.TrimSpace(cmd[2:])
		if newPath == "" {
			m.flash("expected path: :w <path>")
			return m, nil
		}
		return m, m.saveCmd(newPath)
	}

	switch cmd {
	case "q":
		if m.dirty {
			m.flash("unsaved changes — use :q! to force or :wq")
			return m, nil
		}
		return m, tea.Quit
	case "q!":
		return m, tea.Quit
	case "wq", "x":
		return m, tea.Sequence(m.saveCmd(""), tea.Quit)
	case "":
		return m, nil
	}
	if strings.HasPrefix(cmd, "e ") {
		m.flash(":e not yet implemented in v1")
		return m, nil
	}
	m.flash("unknown command: :" + cmd)
	return m, nil
}

// saveCmd returns a tea.Cmd that atomically writes the current buffer to disk.
func (m Model) saveCmd(overridePath string) tea.Cmd {
	path := m.path
	if overridePath != "" {
		path = overridePath
	}
	content := []byte(m.source)
	if m.mode == ModeEdit || m.mode == ModeSplit {
		content = []byte(m.editor.Value())
	}
	return func() tea.Msg {
		err := gfs.AtomicWrite(path, content)
		if err == nil && overridePath != "" {
			return saveDoneMsg{err: nil, newPath: overridePath}
		}
		return saveDoneMsg{err: err}
	}
}

// reloadFromDisk discards unsaved changes and reloads the file from disk.
func (m Model) reloadFromDisk() (tea.Model, tea.Cmd) {
	if m.path == "" {
		return m, nil
	}
	if m.dirty {
		m.flash("buffer dirty — :w first, or press shift+R to discard")
		return m, nil
	}
	b, err := readAll(m.path)
	if err != nil {
		m.flash("reload failed: " + err.Error())
		return m, nil
	}
	m.source = string(b)
	m.editor.SetValue(m.source)
	m.tocItems = ExtractTOC(m.source)
	m.externalChange = false
	return m, m.renderNow()
}
