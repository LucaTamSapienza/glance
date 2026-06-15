package app

import "testing"

func TestSelectionText(t *testing.T) {
	src := "one\ntwo\nthree\nfour"
	tests := []struct {
		name     string
		a, b     int
		wantText string
		wantN    int
	}{
		{"single line", 1, 1, "two", 1},
		{"range", 1, 2, "two\nthree", 2},
		{"reversed order", 2, 0, "one\ntwo\nthree", 3},
		{"whole file", 0, 3, "one\ntwo\nthree\nfour", 4},
		{"clamps out of range", 2, 99, "three\nfour", 2},
		{"clamps negative", -5, 0, "one", 1},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got, n := selectionText(src, tt.a, tt.b)
			if got != tt.wantText {
				t.Errorf("text = %q, want %q", got, tt.wantText)
			}
			if n != tt.wantN {
				t.Errorf("n = %d, want %d", n, tt.wantN)
			}
		})
	}
}
