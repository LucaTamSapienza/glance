package app

import "strings"

// blankMarker is a unique paragraph string inserted by expandBlankLines to
// represent extra blank lines that Goldmark would otherwise normalize away.
// It is removed from the rendered output by restoreExpandedBlanks.
const blankMarker = "GLANCEBLANK"

// preventSetextFromThematicBreaks inserts a blank line before any line that is
// a valid CommonMark thematic break (≥3 of -, *, or _) but directly follows a
// non-blank text line. Without this insertion, Goldmark interprets "text\n---"
// as a setext H2 heading rather than a paragraph followed by a horizontal rule.
func preventSetextFromThematicBreaks(src string) string {
	lines := strings.Split(src, "\n")
	out := make([]string, 0, len(lines)+4)
	for i, line := range lines {
		if i > 0 && isThematicBreak(line) && len(out) > 0 && strings.TrimSpace(out[len(out)-1]) != "" {
			out = append(out, "")
		}
		out = append(out, line)
	}
	return strings.Join(out, "\n")
}

// isThematicBreak reports whether line is a CommonMark thematic break:
// at least 3 of the same character (-, *, or _) with optional interspersed spaces.
func isThematicBreak(line string) bool {
	trimmed := strings.TrimSpace(line)
	if len(trimmed) < 3 {
		return false
	}
	c := trimmed[0]
	if c != '-' && c != '*' && c != '_' {
		return false
	}
	count := 0
	for i := 0; i < len(trimmed); i++ {
		switch trimmed[i] {
		case c:
			count++
		case ' ', '\t':
			// spaces allowed between thematic-break chars
		default:
			return false
		}
	}
	return count >= 3
}

// expandBlankLines replaces runs of N≥2 consecutive blank lines between
// content blocks (outside fenced code blocks) with N-1 blankMarker paragraphs
// separated by single blank lines. Goldmark would otherwise collapse any run
// of blank lines to a single paragraph separator.
//
// Math: each inserted placeholder becomes its own paragraph in the rendered
// output, surrounded by Glamour's inter-paragraph blank line. Removing the
// placeholder lines (restoreExpandedBlanks) leaves exactly N blank lines in
// the final output.
func expandBlankLines(src string) string {
	lines := strings.Split(src, "\n")
	out := make([]string, 0, len(lines)+8)
	blanks := 0
	seenContent := false
	inFence := false

	flush := func() {
		if blanks == 0 {
			return
		}
		if seenContent && blanks >= 2 {
			out = append(out, "")
			for i := 1; i < blanks; i++ {
				out = append(out, blankMarker, "")
			}
		} else {
			for i := 0; i < blanks; i++ {
				out = append(out, "")
			}
		}
		blanks = 0
	}

	for _, line := range lines {
		trimmed := strings.TrimLeft(line, " \t")
		if strings.HasPrefix(trimmed, "```") || strings.HasPrefix(trimmed, "~~~") {
			flush()
			inFence = !inFence
			seenContent = true
			out = append(out, line)
			continue
		}
		if !inFence && strings.TrimSpace(line) == "" {
			blanks++
			continue
		}
		flush()
		seenContent = true
		out = append(out, line)
	}
	// Trailing blanks are preserved as-is (handled by countTrailingBlankLines).
	for i := 0; i < blanks; i++ {
		out = append(out, "")
	}
	return strings.Join(out, "\n")
}

// restoreExpandedBlanks removes blankMarker lines from the Glamour-rendered
// output, converting the surrounding blank lines back into the original
// multi-blank gap. Must be called before the leading/trailing blank restoration.
func restoreExpandedBlanks(rendered string) string {
	lines := strings.Split(rendered, "\n")
	out := lines[:0]
	for _, line := range lines {
		plain := strings.TrimSpace(ansiRE.ReplaceAllString(line, ""))
		if plain == blankMarker {
			continue
		}
		out = append(out, line)
	}
	return strings.Join(out, "\n")
}

// countLeadingBlankLines returns the number of blank lines at the start of s.
// A bare "\n" (just a line terminator) counts as 0; "\n\n" counts as 1.
func countLeadingBlankLines(s string) int {
	n := len(s) - len(strings.TrimLeft(s, "\n"))
	// For an all-blank document (no non-space content), treat the last \n as
	// the document terminator and subtract it from the count.
	if strings.TrimSpace(s) == "" {
		if n <= 1 {
			return 0
		}
		return n - 1
	}
	return n
}

// countTrailingBlankLines returns the number of blank lines at the end of s,
// not counting the line terminator of the last non-blank line.
// Examples:
//
//	"text\n"     → 0  (the trailing \n is just the line terminator)
//	"text\n\n"   → 1
//	"text\n\n\n" → 2
//	"\n"         → 0  (just a terminator, no blank-line content)
//	"\n\n"       → 1
func countTrailingBlankLines(s string) int {
	trimmed := strings.TrimRight(s, "\n")
	n := len(s) - len(trimmed) // number of trailing \n characters
	if n <= 1 {
		return 0
	}
	return n - 1
}
