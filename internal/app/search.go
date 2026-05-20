package app

import (
	"regexp"
	"strings"
	"unicode/utf8"
)

// ansiRE matches CSI escape sequences (e.g. colour codes emitted by glamour).
var ansiRE = regexp.MustCompile(`\x1b\[[0-9;]*[A-Za-z]`)

const (
	searchHL    = "\x1b[48;5;226m\x1b[38;5;232m" // bright yellow bg + near-black text
	searchHLOff = "\x1b[49m\x1b[39m"             // reset background and foreground
)

// hitPos records one search match: the rendered line number and the rune
// column (in the ANSI-stripped text) where the match starts.
type hitPos struct{ line, col int }

// findHits returns one hitPos per occurrence so the counter matches the number
// of highlighted words and jumping moves the column cursor to the match start.
// ANSI codes are stripped before comparing so glamour colour codes never mask a match.
func findHits(rendered, query string) []hitPos {
	if query == "" {
		return nil
	}
	q := strings.ToLower(query)
	qBytes := len(q)
	var hits []hitPos
	for lineNum, line := range strings.Split(rendered, "\n") {
		plain := ansiRE.ReplaceAllString(line, "")
		lowerPlain := strings.ToLower(plain)
		pos := 0
		for {
			idx := strings.Index(lowerPlain[pos:], q)
			if idx < 0 {
				break
			}
			byteOff := pos + idx
			col := utf8.RuneCountInString(lowerPlain[:byteOff])
			hits = append(hits, hitPos{line: lineNum, col: col})
			pos = byteOff + qBytes
			if pos >= len(lowerPlain) {
				break
			}
		}
	}
	return hits
}

// highlightMatches injects yellow-background ANSI codes around every
// case-insensitive occurrence of query inside an ANSI-formatted line.
// It re-applies the highlight after any SGR reset emitted by glamour so
// the yellow background is never clobbered mid-match.
func highlightMatches(line, query string) string {
	if query == "" {
		return line
	}

	// Find match byte-spans in the plain (ANSI-stripped) text.
	plain := ansiRE.ReplaceAllString(line, "")
	lowerPlain := strings.ToLower(plain)
	lowerQuery := strings.ToLower(query)

	if !strings.Contains(lowerPlain, lowerQuery) {
		return line
	}

	qLen := len(lowerQuery)
	type span struct{ start, end int }
	var spans []span
	for pos := 0; pos <= len(lowerPlain)-qLen; {
		idx := strings.Index(lowerPlain[pos:], lowerQuery)
		if idx < 0 {
			break
		}
		start := pos + idx
		spans = append(spans, span{start, start + qLen})
		pos = start + qLen
	}
	if len(spans) == 0 {
		return line
	}

	var out strings.Builder
	plainPos := 0 // byte offset in the plain (stripped) text
	spanIdx := 0
	inHL := false

	for i := 0; i < len(line); {
		// Consume an ANSI escape sequence without advancing plainPos.
		if line[i] == '\x1b' && i+1 < len(line) && line[i+1] == '[' {
			j := i + 2
			for j < len(line) && !isASCIILetter(line[j]) {
				j++
			}
			if j < len(line) {
				j++ // include the terminating letter
			}
			out.WriteString(line[i:j])
			// Re-apply highlight after any SGR sequence (ends with 'm') so
			// that glamour resets don't clear the yellow background mid-match.
			if inHL && j > 0 && line[j-1] == 'm' {
				out.WriteString(searchHL)
			}
			i = j
			continue
		}

		// At each text character, open/close highlights on span boundaries.
		if inHL && spanIdx < len(spans) && plainPos >= spans[spanIdx].end {
			out.WriteString(searchHLOff)
			inHL = false
			spanIdx++
		}
		if !inHL && spanIdx < len(spans) && plainPos == spans[spanIdx].start {
			out.WriteString(searchHL)
			inHL = true
		}

		r, size := utf8.DecodeRuneInString(line[i:])
		out.WriteRune(r)
		plainPos += size
		i += size
	}

	if inHL {
		out.WriteString(searchHLOff)
	}
	return out.String()
}

// injectColCursor places a bold+underline cursor at rune position col inside
// an ANSI-formatted line, clamped to the actual plain-text length.
// Bold+underline is used instead of reverse-video because reverse-video
// becomes invisible on dark terminals when a yellow-bg highlight is active.
func injectColCursor(line string, col int) string {
	plain := []rune(ansiRE.ReplaceAllString(line, ""))
	if len(plain) == 0 {
		return line + "\x1b[1m\x1b[4m \x1b[22m\x1b[24m"
	}
	col = clamp(col, 0, len(plain)-1)

	var out strings.Builder
	runePos := 0

	for i := 0; i < len(line); {
		if line[i] == '\x1b' && i+1 < len(line) && line[i+1] == '[' {
			j := i + 2
			for j < len(line) && !isASCIILetter(line[j]) {
				j++
			}
			if j < len(line) {
				j++
			}
			out.WriteString(line[i:j])
			i = j
			continue
		}
		r, size := utf8.DecodeRuneInString(line[i:])
		if runePos == col {
			out.WriteString("\x1b[1m\x1b[4m") // bold + underline
			out.WriteRune(r)
			out.WriteString("\x1b[22m\x1b[24m") // bold off + underline off (colors untouched)
		} else {
			out.WriteRune(r)
		}
		runePos++
		i += size
	}
	return out.String()
}

func isASCIILetter(b byte) bool {
	return (b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z')
}
