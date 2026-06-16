package app

import "testing"

func TestExtractTOC(t *testing.T) {
	md := "# A\n\nintro\n\n## B\n\nstuff\n\n```\n# not a heading\n```\n\n### C\n"
	items := ExtractTOC(md)
	if len(items) != 3 {
		t.Fatalf("want 3 items, got %d: %+v", len(items), items)
	}
	want := []struct {
		level int
		title string
	}{{1, "A"}, {2, "B"}, {3, "C"}}
	for i, w := range want {
		if items[i].Level != w.level || items[i].Title != w.title {
			t.Errorf("item %d: got %+v, want level=%d title=%q", i, items[i], w.level, w.title)
		}
	}
}

func TestExtractTOCSkipsFenceHashes(t *testing.T) {
	md := "```\n# fake\n```\n"
	if got := ExtractTOC(md); len(got) != 0 {
		t.Fatalf("want 0 items, got %+v", got)
	}
}

func TestExtractTOCIgnoresLevelBeyondSix(t *testing.T) {
	md := "####### too deep\n"
	if got := ExtractTOC(md); len(got) != 0 {
		t.Fatalf("want 0 items, got %+v", got)
	}
}
