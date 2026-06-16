package fs

import (
	"os"
	"path/filepath"
	"testing"
)

func TestAtomicWriteCreatesAndReplaces(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "note.md")
	if err := AtomicWrite(path, []byte("hello")); err != nil {
		t.Fatal(err)
	}
	b, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	if string(b) != "hello" {
		t.Fatalf("got %q", b)
	}
	if err := AtomicWrite(path, []byte("world")); err != nil {
		t.Fatal(err)
	}
	b, _ = os.ReadFile(path)
	if string(b) != "world" {
		t.Fatalf("got %q", b)
	}
}

func TestAtomicWritePreservesModeOnReplace(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "note.md")
	if err := os.WriteFile(path, []byte("a"), 0o600); err != nil {
		t.Fatal(err)
	}
	if err := AtomicWrite(path, []byte("b")); err != nil {
		t.Fatal(err)
	}
	info, err := os.Stat(path)
	if err != nil {
		t.Fatal(err)
	}
	if info.Mode().Perm() != 0o600 {
		t.Fatalf("mode = %o, want 0600", info.Mode().Perm())
	}
}

func TestAtomicWriteEmptyPath(t *testing.T) {
	if err := AtomicWrite("", []byte("x")); err == nil {
		t.Fatal("want error on empty path")
	}
}
