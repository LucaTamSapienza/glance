package render

import (
	"regexp"
	"strings"
	"testing"
)

// ansiRE strips SGR escape sequences so tests can assert on plain text.
var ansiRE = regexp.MustCompile("\x1b\\[[0-9;]*m")

func TestPreservedNewlines(t *testing.T) {
	g, err := NewGlamour(80)
	if err != nil {
		t.Fatalf("NewGlamour: %v", err)
	}
	out, err := g.Render("first line\nsecond line\n")
	if err != nil {
		t.Fatalf("Render: %v", err)
	}
	plain := ansiRE.ReplaceAllString(out, "")
	if strings.Contains(plain, "first line second line") {
		t.Errorf("newlines collapsed into a space; want them preserved:\n%q", plain)
	}
	if !strings.Contains(plain, "first line") || !strings.Contains(plain, "second line") {
		t.Errorf("rendered output missing content:\n%q", plain)
	}
}
