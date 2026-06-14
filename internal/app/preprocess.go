package app

import (
	"regexp"
	"strings"
)

// boldStarRE / boldUnderscoreRE match a bold span whose inner edges may carry
// stray spaces (e.g. "** word **"). The capture is the whole interior,
// including any surrounding spaces, so tightenEmphasis can trim it. The inner
// run excludes the delimiter char so two adjacent bold spans don't merge and a
// "***" run is never mistaken for an empty bold span.
var (
	boldStarRE       = regexp.MustCompile(`\*\*([ \t]*[^*]*?[ \t]*)\*\*`)
	boldUnderscoreRE = regexp.MustCompile(`__([ \t]*[^_]*?[ \t]*)__`)
)

// tightenBoldDelimiters removes spaces that sit directly inside bold delimiters
// so "** parola **" renders as bold. CommonMark requires the delimiter to hug
// the text ("**parola**"); glance is more forgiving. Only bold (** ** and
// __ __) is touched — single-star/underscore italic is left alone to avoid
// clashing with bullet lists. Fenced code blocks and inline code spans are
// preserved verbatim.
func tightenBoldDelimiters(src string) string {
	lines := strings.Split(src, "\n")
	inFence := false
	for idx, line := range lines {
		trimmed := strings.TrimLeft(line, " \t")
		if strings.HasPrefix(trimmed, "```") || strings.HasPrefix(trimmed, "~~~") {
			inFence = !inFence
			continue
		}
		if inFence {
			continue
		}
		lines[idx] = tightenBoldOutsideCode(line)
	}
	return strings.Join(lines, "\n")
}

// tightenBoldOutsideCode applies the bold tightening only to the parts of a
// line that are not inside an inline code span (`...`). Backtick runs are
// matched by equal length, mirroring CommonMark's code-span rule.
func tightenBoldOutsideCode(line string) string {
	var b strings.Builder
	i, n := 0, len(line)
	for i < n {
		if line[i] == '`' {
			j := i
			for j < n && line[j] == '`' {
				j++
			}
			runLen := j - i
			if close := indexBacktickRun(line, j, runLen); close >= 0 {
				b.WriteString(line[i : close+runLen]) // code span, verbatim
				i = close + runLen
				continue
			}
			b.WriteString(line[i:j]) // unterminated run: literal backticks
			i = j
			continue
		}
		start := i
		for i < n && line[i] != '`' {
			i++
		}
		seg := line[start:i]
		seg = tightenEmphasis(seg, "**", boldStarRE)
		seg = tightenEmphasis(seg, "__", boldUnderscoreRE)
		b.WriteString(seg)
	}
	return b.String()
}

// indexBacktickRun returns the start index of the next run of exactly runLen
// backticks at or after from, or -1 if there is none.
func indexBacktickRun(s string, from, runLen int) int {
	for i := from; i < len(s); {
		if s[i] == '`' {
			j := i
			for j < len(s) && s[j] == '`' {
				j++
			}
			if j-i == runLen {
				return i
			}
			i = j
		} else {
			i++
		}
	}
	return -1
}

// tightenEmphasis trims spaces directly inside each delim..delim span matched by
// re. A span that is empty after trimming (e.g. "** **") is left untouched.
func tightenEmphasis(s, delim string, re *regexp.Regexp) string {
	return re.ReplaceAllStringFunc(s, func(m string) string {
		inner := m[len(delim) : len(m)-len(delim)]
		trimmed := strings.TrimSpace(inner)
		if trimmed == "" || trimmed == inner {
			return m
		}
		return delim + trimmed + delim
	})
}

// blankMarker is a unique paragraph string inserted by expandBlankLines to
// represent extra blank lines that Goldmark would otherwise normalize away.
// It is removed from the rendered output by restoreExpandedBlanks.
const blankMarker = "GLANCEBLANK"

// preventSetextFromThematicBreaks inserts a blank line before any line that
// would otherwise combine with the preceding non-blank text line into a setext
// heading. This covers two cases:
//
//   - thematic breaks (≥3 of -, *, or _): "text\n---" should render as a
//     paragraph followed by a horizontal rule, not a setext H2.
//   - setext underlines (a run of only - or only =, any length): "text\n-" or
//     "text\n===" should stay a paragraph rather than turn the text above into
//     a heading. glance prefers explicit ATX headings (# / ##) so a stray
//     dash/equals line never silently rewrites the text above it.
func preventSetextFromThematicBreaks(src string) string {
	lines := strings.Split(src, "\n")
	out := make([]string, 0, len(lines)+4)
	for i, line := range lines {
		if i > 0 && len(out) > 0 && strings.TrimSpace(out[len(out)-1]) != "" &&
			(isThematicBreak(line) || isSetextUnderline(line)) {
			out = append(out, "")
		}
		out = append(out, line)
	}
	return strings.Join(out, "\n")
}

// isSetextUnderline reports whether line is a CommonMark setext heading
// underline: a non-empty run of only '-' or only '=' characters, with optional
// leading/trailing spaces but no interior spaces. A single character qualifies
// (CommonMark imposes no minimum length), so "-" and "=" both match.
func isSetextUnderline(line string) bool {
	trimmed := strings.TrimSpace(line)
	if trimmed == "" {
		return false
	}
	c := trimmed[0]
	if c != '-' && c != '=' {
		return false
	}
	for i := 0; i < len(trimmed); i++ {
		if trimmed[i] != c {
			return false
		}
	}
	return true
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
