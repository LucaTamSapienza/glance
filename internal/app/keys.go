package app

import "github.com/charmbracelet/bubbles/key"

type KeyMap struct {
	Quit       key.Binding
	Help       key.Binding
	EditMode   key.Binding
	ReaderMode key.Binding
	TOC        key.Binding
	Search     key.Binding
	NextHit    key.Binding
	PrevHit    key.Binding
	Top        key.Binding
	Bottom     key.Binding
	Down       key.Binding
	Up         key.Binding
	Left       key.Binding
	Right      key.Binding
	PageDown   key.Binding
	PageUp     key.Binding
	Command    key.Binding
	OpenLink   key.Binding
}

func DefaultKeys() KeyMap {
	return KeyMap{
		Quit:       key.NewBinding(key.WithKeys("q"), key.WithHelp("q", "quit")),
		Help:       key.NewBinding(key.WithKeys("?"), key.WithHelp("?", "help")),
		EditMode:   key.NewBinding(key.WithKeys("e"), key.WithHelp("e", "edit")),
		ReaderMode: key.NewBinding(key.WithKeys("esc"), key.WithHelp("esc", "reader")),
		TOC:        key.NewBinding(key.WithKeys("t"), key.WithHelp("t", "toc")),
		Search:     key.NewBinding(key.WithKeys("/"), key.WithHelp("/", "search")),
		NextHit:    key.NewBinding(key.WithKeys("n"), key.WithHelp("n", "next match")),
		PrevHit:    key.NewBinding(key.WithKeys("N"), key.WithHelp("N", "prev match")),
		Top:        key.NewBinding(key.WithKeys("g", "home"), key.WithHelp("gg", "top")),
		Bottom:     key.NewBinding(key.WithKeys("G", "end"), key.WithHelp("G", "bottom")),
		Down:       key.NewBinding(key.WithKeys("j", "down"), key.WithHelp("j/↓", "down")),
		Up:         key.NewBinding(key.WithKeys("k", "up"), key.WithHelp("k/↑", "up")),
		Left:       key.NewBinding(key.WithKeys("h", "left", "alt+left", "alt+b"), key.WithHelp("h/←", "left")),
		Right:      key.NewBinding(key.WithKeys("l", "right", "alt+right", "alt+f"), key.WithHelp("l/→", "right")),
		PageDown:   key.NewBinding(key.WithKeys("pgdown", "ctrl+f", " "), key.WithHelp("pgdn", "page down")),
		PageUp:     key.NewBinding(key.WithKeys("pgup", "ctrl+b"), key.WithHelp("pgup", "page up")),
		Command:    key.NewBinding(key.WithKeys(":"), key.WithHelp(":", "command")),
		OpenLink:   key.NewBinding(key.WithKeys("enter"), key.WithHelp("enter", "open link")),
	}
}
