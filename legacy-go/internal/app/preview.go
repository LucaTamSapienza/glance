package app

import (
	"strings"
	"time"

	tea "github.com/charmbracelet/bubbletea"
	"github.com/LucaTamSapienza/glance/internal/render"
)

// renderNow issues an immediate (non-debounced) render command.
func (m *Model) renderNow() tea.Cmd {
	m.previewGen++
	return m.renderCmd(m.previewGen)
}

// debouncedRender schedules a render after debounceInterval, discarding any
// pending renders with a stale generation number.
func (m *Model) debouncedRender() tea.Cmd {
	m.previewGen++
	gen := m.previewGen
	return tea.Tick(debounceInterval, func(time.Time) tea.Msg {
		return debounceMsg{gen: gen}
	})
}

// renderCmd builds a tea.Cmd that renders the current source in a goroutine
// and returns a previewReadyMsg. Stale results are discarded via the gen field.
func (m *Model) renderCmd(gen int) tea.Cmd {
	rawSrc := m.source
	if m.mode == ModeSplit || m.mode == ModeEdit {
		rawSrc = m.editor.Value()
	}

	// Capture blank-line context before any transformation.
	leading := countLeadingBlankLines(rawSrc)
	trailing := countTrailingBlankLines(rawSrc)

	// Preprocess: fix setext-heading ambiguity, then expand multi-blank gaps
	// so Goldmark (which normalizes all blank sequences to 1) preserves the
	// user's exact blank-line count between content blocks.
	src := preventSetextFromThematicBreaks(rawSrc)
	src = tightenBoldDelimiters(src)
	src = expandBlankLines(src)

	// Ensure Goldmark always sees a newline-terminated document so the last
	// ATX heading (e.g. "# a") is parsed correctly.
	if !strings.HasSuffix(src, "\n") {
		src += "\n"
	}

	w := m.previewWidth()
	// Create the renderer in the Update goroutine so the async closure only
	// calls r.Render — glamour/goldmark initialisation touches global chroma
	// registry state that is not documented as goroutine-safe.
	r, err := render.NewGlamour(w)
	if err != nil {
		e := err
		return func() tea.Msg {
			return previewReadyMsg{gen: gen, rendered: "render error: " + e.Error()}
		}
	}
	return func() tea.Msg {
		out, renderErr := r.Render(src)
		if renderErr != nil {
			return previewReadyMsg{gen: gen, rendered: "render error: " + renderErr.Error(), renderer: r}
		}
		// Strip Glamour's own leading blank(s) (max 2), then restore the
		// source's leading blank lines so the view matches the editor.
		stripped := 0
		for stripped < 2 && strings.HasPrefix(out, "\n") {
			out = out[1:]
			stripped++
		}
		if leading > 0 {
			out = strings.Repeat("\n", leading) + out
		}
		// Remove the placeholder lines inserted by expandBlankLines; the
		// surrounding blank lines become the multi-blank gap the user typed.
		out = restoreExpandedBlanks(out)
		// Strip Glamour's trailing padding, then restore the source's trailing
		// blank lines.
		out = strings.TrimRight(out, "\n")
		if out == "" {
			out = " "
		}
		if trailing > 0 {
			out += strings.Repeat("\n", trailing)
		}
		// 1:1 row alignment: pad the rendered output so each source line i
		// lives on rendered row srcMap[i] (≈ i, modulo soft-wrap).
		normalized, srcMap := normalizeRendered(rawSrc, out)
		return previewReadyMsg{gen: gen, rendered: normalized, renderer: r, srcMap: srcMap}
	}
}

// previewWidth returns the width (in terminal columns) available for the
// rendered preview, accounting for mode, cursor prefix, and open TOC panel.
func (m *Model) previewWidth() int {
	w := m.width
	if m.mode == ModeReader {
		w -= cursorColWidth
	}
	if m.tocOpen && m.mode == ModeReader {
		w -= tocWidth
	}
	if m.mode == ModeSplit {
		w = (m.width - 1) / 2
	}
	if w < 20 {
		w = 20
	}
	return w
}

// layout recalculates viewport/editor dimensions and recreates the renderer
// when the terminal size or mode changes.
func (m *Model) layout() {
	if m.width == 0 || m.height == 0 {
		return
	}
	bodyH := m.height - statusHeight
	if m.cmdActive || m.searchActive {
		bodyH--
	}
	if bodyH < 3 {
		bodyH = 3
	}

	switch m.mode {
	case ModeReader:
		w := m.width - cursorColWidth
		if m.tocOpen {
			w -= tocWidth
		}
		m.reader.Width = w
		m.reader.Height = bodyH
	case ModeEdit:
		m.editor.SetWidth(m.width)
		m.editor.SetHeight(bodyH)
	case ModeSplit:
		half := (m.width - 1) / 2
		m.editor.SetWidth(half)
		m.editor.SetHeight(bodyH)
		m.reader.Width = m.width - half - 1
		m.reader.Height = bodyH
	}
	if m.renderer == nil || m.renderer.Width() != m.previewWidth() {
		r, err := render.NewGlamour(m.previewWidth())
		if err == nil {
			m.renderer = r
		}
	}
}
