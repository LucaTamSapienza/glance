package app

import (
	"time"

	"github.com/charmbracelet/bubbles/textarea"
	"github.com/charmbracelet/bubbles/textinput"
	"github.com/charmbracelet/bubbles/viewport"
	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"

	gfs "github.com/lucatam/glance/internal/fs"
	"github.com/lucatam/glance/internal/render"
)

// Mode is the current editing/viewing mode.
type Mode int

const (
	ModeReader Mode = iota // rendered preview with cursor
	ModeEdit               // full-screen raw text editor
	ModeSplit              // editor on left + live preview on right
)

const (
	debounceInterval = 80 * time.Millisecond
	tocWidth         = 28
	statusHeight     = 1
	cursorColWidth   = 2 // "❯ " prefix reserved in reader mode
)

// Internal Bubbletea messages.
type (
	previewReadyMsg struct {
		gen      int
		rendered string
		renderer *render.Glamour
		srcMap   []int
	}
	saveDoneMsg struct {
		err     error
		newPath string
	}
	externalChangeMsg struct{}
	debounceMsg       struct{ gen int }
)

// Model is the root Bubbletea model for glance.
type Model struct {
	path   string
	source string // last-saved-to-disk content (Reader); live textarea buffer (Edit/Split)
	dirty  bool
	mode   Mode
	width  int
	height int

	renderer *render.Glamour
	reader   viewport.Model // rendered markdown in Reader mode and Split right pane
	editor   textarea.Model
	cmdLine  textinput.Model
	search   textinput.Model

	keys        KeyMap
	tocOpen     bool
	tocFocused  bool
	tocSelected int
	tocItems    []TOCItem

	cmdActive    bool
	searchActive bool
	searchHits   []hitPos
	searchIdx    int

	status        string
	statusExpires time.Time
	err           error

	// cursor tracking in reader mode
	cursorLine int
	cursorCol  int
	totalLines int

	// authoritative cursor position in *source* coordinates (line/col are
	// indices into the source string, not the rendered view). Reader→Editor
	// transitions use these directly instead of the readerLineToSource
	// heuristic. Kept in sync by Tasks 7 & 8.
	srcLine int
	srcCol  int

	// debounce generation: only the latest preview is applied
	previewGen int

	// full rendered content (used by search)
	rendered string

	// pending double-key (gg)
	pendingG bool

	// file watcher
	watchCh <-chan gfs.ChangeEvent

	// set when an external file change notice arrives
	externalChange bool

	helpOpen bool

	// cursor position to sync after the next render completes
	pendingSyncLine int
	pendingSyncCol  int

	// Source-line → rendered-row mapping, rebuilt after every render.
	srcToRendered []int
}

// New creates the initial Model for the given file path, content and mode.
func New(path string, content []byte, mode Mode) Model {
	src := string(content)

	ta := textarea.New()
	ta.Placeholder = "Type Markdown…"
	ta.ShowLineNumbers = true
	ta.CharLimit = 0
	ta.SetValue(src)
	ta.Prompt = ""
	ta.FocusedStyle.CursorLine = lipgloss.NewStyle()
	if mode == ModeReader {
		ta.Blur()
	} else {
		ta.Focus()
	}

	cli := textinput.New()
	cli.Prompt = ":"
	cli.CharLimit = 256

	se := textinput.New()
	se.Prompt = "/"
	se.Placeholder = "search rendered text…"
	se.CharLimit = 256

	vp := viewport.New(80, 24)
	vp.MouseWheelEnabled = true

	m := Model{
		path:            path,
		source:          src,
		mode:            mode,
		reader:          vp,
		editor:          ta,
		cmdLine:         cli,
		search:          se,
		keys:            DefaultKeys(),
		tocItems:        ExtractTOC(src),
		pendingSyncLine: -1,
	}
	if path != "" {
		if ch, _, err := gfs.Watch(path); err == nil {
			m.watchCh = ch
		}
	}
	return m
}

// Init implements tea.Model.
func (m Model) Init() tea.Cmd {
	cmds := []tea.Cmd{m.waitWatch()}
	if m.mode != ModeReader {
		cmds = append(cmds, textarea.Blink)
	}
	return tea.Batch(cmds...)
}

// waitWatch returns a command that blocks until the file watcher fires.
func (m Model) waitWatch() tea.Cmd {
	ch := m.watchCh
	if ch == nil {
		return nil
	}
	return func() tea.Msg {
		_, ok := <-ch
		if !ok {
			return nil
		}
		return externalChangeMsg{}
	}
}
