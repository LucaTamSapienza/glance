package fs

import (
	"path/filepath"

	"github.com/fsnotify/fsnotify"
)

type ChangeEvent struct{ Path string }

func Watch(path string) (<-chan ChangeEvent, func() error, error) {
	w, err := fsnotify.NewWatcher()
	if err != nil {
		return nil, nil, err
	}
	abs, err := filepath.Abs(path)
	if err != nil {
		_ = w.Close()
		return nil, nil, err
	}
	if err := w.Add(filepath.Dir(abs)); err != nil {
		_ = w.Close()
		return nil, nil, err
	}
	ch := make(chan ChangeEvent, 4)
	go func() {
		defer close(ch)
		for {
			select {
			case ev, ok := <-w.Events:
				if !ok {
					return
				}
				if ev.Name != abs {
					continue
				}
				if ev.Op&(fsnotify.Write|fsnotify.Create|fsnotify.Rename) == 0 {
					continue
				}
				ch <- ChangeEvent{Path: ev.Name}
			case _, ok := <-w.Errors:
				if !ok {
					return
				}
			}
		}
	}()
	return ch, w.Close, nil
}
