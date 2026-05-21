package app

import (
	"reflect"
	"testing"
)

func TestBuildSrcToRenderedExactMatch(t *testing.T) {
	// Each source line appears verbatim in rendered (no styling, no wrap).
	source := "alpha\nbeta\ngamma"
	rendered := "alpha\nbeta\ngamma"
	got := buildSrcToRendered(source, rendered)
	want := []int{0, 1, 2}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("want %v, got %v", want, got)
	}
}

func TestBuildSrcToRenderedCollapsedBlank(t *testing.T) {
	// Matches the user's screenshot: source has a blank between heading and
	// paragraph, rendered collapses it.
	source := "# Ciao\n## io sono Luca\n\n**tu come ti chiami**\n\nsssss"
	// Glamour-style rendered output (no styling for the test, just text):
	// the blank between heading and paragraph is gone; the blank between
	// paragraph and "sssss" is preserved.
	rendered := "Ciao\nio sono Luca\ntu come ti chiami\n\nsssss"
	got := buildSrcToRendered(source, rendered)
	// Source lines: 0=#Ciao, 1=##io, 2=blank, 3=**tu**, 4=blank, 5=sssss
	// Expected map: 0→0, 1→1, 2→1 (blank collapsed onto preceding line),
	//               3→2, 4→3, 5→4.
	want := []int{0, 1, 1, 2, 3, 4}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("want %v, got %v", want, got)
	}
}

func TestBuildSrcToRenderedStripsAnsi(t *testing.T) {
	source := "hello"
	rendered := "\x1b[1mhello\x1b[0m"
	got := buildSrcToRendered(source, rendered)
	want := []int{0}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("want %v, got %v", want, got)
	}
}

func TestBuildSrcToRenderedEmptySource(t *testing.T) {
	got := buildSrcToRendered("", "")
	want := []int{0}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("want %v, got %v", want, got)
	}
}

func TestBuildSrcToRenderedConsecutiveBlanks(t *testing.T) {
	// Glamour collapses two consecutive source blanks into one rendered blank.
	source := "Hello\n\n\nWorld"
	rendered := "Hello\n\nWorld"
	got := buildSrcToRendered(source, rendered)
	want := []int{0, 1, 1, 2}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("want %v, got %v", want, got)
	}
}

func TestStripMarkdownTokensOrderedList(t *testing.T) {
	if got := stripMarkdownTokens("1. first item"); got != "first item" {
		t.Errorf("want %q, got %q", "first item", got)
	}
	if got := stripMarkdownTokens("10. tenth item"); got != "tenth item" {
		t.Errorf("want %q, got %q", "tenth item", got)
	}
}
