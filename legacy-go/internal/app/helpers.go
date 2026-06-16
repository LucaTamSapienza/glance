package app

import (
	"os/exec"
	"time"

	tea "github.com/charmbracelet/bubbletea"
)

// flash sets a transient status message that expires after 3 seconds.
func (m *Model) flash(s string) {
	m.status = s
	m.statusExpires = time.Now().Add(3 * time.Second)
}

// jumpToHit scrolls the viewport and moves the cursor to the current search hit.
func (m *Model) jumpToHit() {
	if len(m.searchHits) == 0 {
		return
	}
	hit := m.searchHits[m.searchIdx]
	m.reader.SetYOffset(hit.line)
	m.cursorLine = clamp(hit.line, 0, m.totalLines-1)
	m.cursorCol = hit.col
	m.srcLine = m.renderedLineToSourceLine(m.cursorLine)
	m.srcCol = m.cursorCol
}

// hitLineSet builds a fast-lookup set of rendered line numbers from the
// current searchHits slice.
func (m *Model) hitLineSet() map[int]bool {
	if len(m.searchHits) == 0 {
		return nil
	}
	s := make(map[int]bool, len(m.searchHits))
	for _, h := range m.searchHits {
		s[h.line] = true
	}
	return s
}

// keyMatch reports whether msg matches any of the key binding's keys.
func keyMatch(b interface{ Keys() []string }, msg tea.KeyMsg) bool {
	s := msg.String()
	for _, k := range b.Keys() {
		if k == s {
			return true
		}
	}
	return false
}

// clamp restricts v to the closed interval [lo, hi].
func clamp(v, lo, hi int) int {
	if v < lo {
		return lo
	}
	if v > hi {
		return hi
	}
	return v
}

func max(a, b int) int {
	if a > b {
		return a
	}
	return b
}

// openExternal opens a URL or file path with the macOS `open` command.
// Reserved for v1.1 link-click support.
func openExternal(target string) error {
	return exec.Command("open", target).Start()
}
