package app

import (
	"strings"
	"testing"
)

func TestIsThematicBreak(t *testing.T) {
	cases := []struct {
		line string
		want bool
	}{
		{"---", true},
		{"***", true},
		{"___", true},
		{"- - -", true},
		{"* * *", true},
		{"----", true},
		{"--", false},    // too short
		{"-", false},     // too short
		{"===", false},   // not a valid thematic break char
		{"abc", false},
		{"", false},
		{"  ---  ", true}, // leading/trailing spaces stripped
	}
	for _, tc := range cases {
		got := isThematicBreak(tc.line)
		if got != tc.want {
			t.Errorf("isThematicBreak(%q) = %v, want %v", tc.line, got, tc.want)
		}
	}
}

func TestPreventSetextFromThematicBreaks(t *testing.T) {
	cases := []struct {
		name  string
		input string
		want  string
	}{
		{
			name:  "dash after text gets blank line inserted",
			input: "io mi chiamo luca\n---",
			want:  "io mi chiamo luca\n\n---",
		},
		{
			name:  "dash already after blank line unchanged",
			input: "io mi chiamo luca\n\n---",
			want:  "io mi chiamo luca\n\n---",
		},
		{
			name:  "heading before dash not affected",
			input: "# Ciao\ntext\n---",
			want:  "# Ciao\ntext\n\n---",
		},
		{
			name:  "standalone dash unchanged",
			input: "\n---\n",
			want:  "\n---\n",
		},
		{
			name:  "equals after text not changed (setext H1, not thematic break)",
			input: "My Title\n===",
			want:  "My Title\n===",
		},
		{
			name:  "stars after text get blank line inserted",
			input: "some text\n***",
			want:  "some text\n\n***",
		},
		{
			name:  "multiple thematic breaks handled correctly",
			input: "para1\n---\npara2\n***",
			want:  "para1\n\n---\npara2\n\n***",
		},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			got := preventSetextFromThematicBreaks(tc.input)
			if got != tc.want {
				t.Errorf("got:\n%s\nwant:\n%s", strings.ReplaceAll(got, "\n", "↵\n"), strings.ReplaceAll(tc.want, "\n", "↵\n"))
			}
		})
	}
}

func TestCountTrailingBlankLines(t *testing.T) {
	cases := []struct {
		input string
		want  int
	}{
		{"text\n", 0},
		{"text\n\n", 1},
		{"text\n\n\n", 2},
		{"text\n\n\n\n", 3},
		{"text", 0},
		{"", 0},
		{"\n", 0},
		{"\n\n", 1},
	}
	for _, tc := range cases {
		got := countTrailingBlankLines(tc.input)
		if got != tc.want {
			t.Errorf("countTrailingBlankLines(%q) = %d, want %d", tc.input, got, tc.want)
		}
	}
}

func TestExpandBlankLines(t *testing.T) {
	cases := []struct {
		name      string
		blanksIn  int // blank lines between blocks in input
		blanksOut int // blank lines expected between blocks in output
	}{
		{"1 blank → 1 blank (no expansion)", 1, 1},
		{"2 blanks → 2 blanks", 2, 2},
		{"3 blanks → 3 blanks", 3, 3},
		{"5 blanks → 5 blanks", 5, 5},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			src := "para1" + strings.Repeat("\n", tc.blanksIn+1) + "para2"
			expanded := expandBlankLines(src)

			// Count markers inserted.
			markerCount := strings.Count(expanded, blankMarker)
			wantMarkers := 0
			if tc.blanksIn >= 2 {
				wantMarkers = tc.blanksIn - 1
			}
			if markerCount != wantMarkers {
				t.Errorf("markers: got %d, want %d\nexpanded:\n%s",
					markerCount, wantMarkers, strings.ReplaceAll(expanded, "\n", "↵\n"))
			}

			// After restoring (simulate removing marker lines), count blank lines.
			restored := restoreExpandedBlanks(expanded)
			lines := strings.Split(restored, "\n")
			blanks := 0
			for _, l := range lines[1 : len(lines)-1] {
				if strings.TrimSpace(l) == "" {
					blanks++
				}
			}
			if blanks != tc.blanksOut {
				t.Errorf("blank lines after restore: got %d, want %d\nrestored:\n%s",
					blanks, tc.blanksOut, strings.ReplaceAll(restored, "\n", "↵\n"))
			}
		})
	}
}

func TestExpandBlankLinesSkipsFence(t *testing.T) {
	// Blank lines inside a fenced code block must not be touched.
	src := "para1\n\n\n```\ncode\n\n\nmore\n```\n\n\npara2"
	expanded := expandBlankLines(src)

	// Markers must only appear OUTSIDE the fence.
	fenceOpen := strings.Index(expanded, "```")
	fenceClose := strings.LastIndex(expanded, "```")
	inner := expanded[fenceOpen:fenceClose]
	if strings.Contains(inner, blankMarker) {
		t.Errorf("blankMarker found inside fence:\n%s", strings.ReplaceAll(expanded, "\n", "↵\n"))
	}
	// There must be markers outside the fence.
	outer := expanded[:fenceOpen] + expanded[fenceClose:]
	if !strings.Contains(outer, blankMarker) {
		t.Errorf("expected blankMarker outside fence, got none:\n%s", strings.ReplaceAll(expanded, "\n", "↵\n"))
	}
}

func TestCountLeadingBlankLines(t *testing.T) {
	cases := []struct {
		input string
		want  int
	}{
		{"text", 0},
		{"\ntext", 1},
		{"\n\ntext", 2},
		{"\n\n\ntext\n", 3},
		{"", 0},
		{"\n", 0},   // single newline: the split produces ["",""] — the trailing "" is the terminator
		{"\n\n", 1}, // two newlines: ["","",""] — one blank line
	}
	for _, tc := range cases {
		got := countLeadingBlankLines(tc.input)
		if got != tc.want {
			t.Errorf("countLeadingBlankLines(%q) = %d, want %d", tc.input, got, tc.want)
		}
	}
}
