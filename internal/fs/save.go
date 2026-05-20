package fs

import (
	"fmt"
	"os"
	"path/filepath"
)

func AtomicWrite(path string, data []byte) error {
	if path == "" {
		return fmt.Errorf("no destination path (input was stdin)")
	}
	dir := filepath.Dir(path)
	tmp, err := os.CreateTemp(dir, ".glance-*")
	if err != nil {
		return err
	}
	tmpPath := tmp.Name()
	defer func() { _ = os.Remove(tmpPath) }()

	if _, err := tmp.Write(data); err != nil {
		_ = tmp.Close()
		return err
	}
	if err := tmp.Sync(); err != nil {
		_ = tmp.Close()
		return err
	}
	if err := tmp.Close(); err != nil {
		return err
	}
	if info, err := os.Stat(path); err == nil {
		_ = os.Chmod(tmpPath, info.Mode())
	}
	return os.Rename(tmpPath, path)
}
