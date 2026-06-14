package main

import (
	"flag"
	"fmt"
	"io"
	"os"
	"path/filepath"

	tea "github.com/charmbracelet/bubbletea"
	"github.com/LucaTamSapienza/glance/internal/app"
)

var version = "dev"

func main() {
	showVersion := flag.Bool("version", false, "print version and exit")
	startInEditor := flag.Bool("edit", false, "open directly in split editor mode")
	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, `glance — the Markdown reader/editor for your terminal

Usage:
  glance [flags] <file.md>
  cat file.md | glance

Flags:
`)
		flag.PrintDefaults()
		fmt.Fprintf(os.Stderr, `
Modes:
  Reader  — default view, beautifully rendered Markdown
  Insert  — full-screen raw Markdown editor  (enter: i, exit: Esc)
  Split   — editor on left + live preview on right  (enter: e, exit: Esc)

Reader keys:
  j / k / ↑ / ↓        move cursor line by line
  gg / G                jump to top / bottom
  Space / Ctrl+F        page down
  Ctrl+B                page up
  /                     incremental search (n / N to cycle matches)
  t                     toggle Table of Contents sidebar
  i                     enter Insert mode (full-screen editor)
  e                     enter Split mode  (editor + live preview)
  R                     reload file (after external change)
  ?                     help overlay
  q / Ctrl+C            quit

Insert mode keys:
  Esc                   return to Reader (re-renders document)
  Ctrl+S                save without leaving Insert mode
  : (after Esc)         open command line

Split mode keys:
  Esc                   return to Reader
  i / a / o             focus the editor pane

Command line  (available in Reader and Split):
  :w                    save
  :q                    quit  (:q! to force if unsaved)
  :wq / :x              save and quit

Mouse:
  Wheel                 scroll the reader / preview pane
`)
	}
	flag.Parse()

	if *showVersion {
		fmt.Println("glance", version)
		return
	}

	path, content, err := loadInput(flag.Args())
	if err != nil {
		fmt.Fprintln(os.Stderr, "glance:", err)
		os.Exit(1)
	}

	mode := app.ModeReader
	if *startInEditor {
		mode = app.ModeSplit
	} else if path != "" && len(content) == 0 {
		// New or empty file → drop straight into Insert mode so the user
		// can start typing without having to press `i` first.
		mode = app.ModeEdit
	}

	m := app.New(path, content, mode)
	p := tea.NewProgram(m, tea.WithAltScreen(), tea.WithMouseCellMotion())
	if _, err := p.Run(); err != nil {
		fmt.Fprintln(os.Stderr, "glance:", err)
		os.Exit(1)
	}
}

func loadInput(args []string) (path string, content []byte, err error) {
	if len(args) == 0 {
		stat, _ := os.Stdin.Stat()
		if (stat.Mode() & os.ModeCharDevice) == 0 {
			b, err := io.ReadAll(os.Stdin)
			if err != nil {
				return "", nil, fmt.Errorf("reading stdin: %w", err)
			}
			return "", b, nil
		}
		flag.Usage()
		return "", nil, fmt.Errorf("no file given")
	}
	p, err := filepath.Abs(args[0])
	if err != nil {
		return "", nil, err
	}
	b, err := os.ReadFile(p)
	if err != nil {
		if !os.IsNotExist(err) {
			return "", nil, err
		}
		// File doesn't exist — start with an empty buffer. The parent
		// directory must exist so that :w / ctrl+s can create the file.
		if _, statErr := os.Stat(filepath.Dir(p)); statErr != nil {
			return "", nil, fmt.Errorf("cannot create %s: parent directory does not exist", p)
		}
		return p, nil, nil
	}
	return p, b, nil
}
