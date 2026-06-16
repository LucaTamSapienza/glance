package app

import "testing"

func TestFindHits(t *testing.T) {
	body := "first line\nSecond Line\nthird line about Second things\nfourth"
	hits := findHits(body, "second")
	if len(hits) != 2 {
		t.Fatalf("want 2 hits, got %d: %v", len(hits), hits)
	}
	if hits[0].line != 1 || hits[1].line != 2 {
		t.Errorf("unexpected hit lines: %v", hits)
	}
	// "Second" starts at rune column 0 on line 1, and column 17 on line 2
	if hits[0].col != 0 {
		t.Errorf("hit[0] col: want 0, got %d", hits[0].col)
	}
	if hits[1].col != 17 {
		t.Errorf("hit[1] col: want 17, got %d", hits[1].col)
	}
}

func TestFindHitsEmpty(t *testing.T) {
	if hits := findHits("anything", ""); hits != nil {
		t.Fatalf("want nil for empty query, got %v", hits)
	}
}
