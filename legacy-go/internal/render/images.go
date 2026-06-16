package render

import "os"

type Terminal int

const (
	TerminalUnknown Terminal = iota
	TerminalKitty
	TerminalITerm2
	TerminalWezTerm
	TerminalGhostty
)

func DetectTerminal() Terminal {
	if os.Getenv("KITTY_WINDOW_ID") != "" || os.Getenv("TERM") == "xterm-kitty" {
		return TerminalKitty
	}
	switch os.Getenv("TERM_PROGRAM") {
	case "iTerm.app":
		return TerminalITerm2
	case "WezTerm":
		return TerminalWezTerm
	case "ghostty":
		return TerminalGhostty
	}
	return TerminalUnknown
}

func SupportsInlineImages() bool {
	t := DetectTerminal()
	return t == TerminalKitty || t == TerminalITerm2 || t == TerminalWezTerm || t == TerminalGhostty
}
