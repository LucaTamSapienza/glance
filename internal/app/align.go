package app

import "strings"

// buildSrcToRendered produces a mapping from source line index to the
// rendered row that contains (or most closely corresponds to) that source
// line. The greedy sweep walks both arrays once: for each source line it
// finds the first rendered row at or after the current rendered cursor
// whose ANSI-stripped text matches the source line's markdown-stripped
// text.
//
// Blank source lines have two cases:
//   - If the current rendered cursor row is also blank, the blank source
//     line maps to that row and the cursor advances past it.
//   - Otherwise the blank source line maps to the most recently consumed
//     rendered row (lastNonBlankRendered). This handles Glamour collapsing
//     a source blank (e.g. between a heading and a paragraph) — the source
//     blank lines up with the content row it sat next to.
//
// The match test is lenient (substring either direction) so styled output
// like "*bold*" → "bold" or "[link](url)" → "link" still aligns.
func buildSrcToRendered(source, rendered string) []int {
	srcLines := strings.Split(source, "\n")
	renderedLines := strings.Split(rendered, "\n")
	out := make([]int, len(srcLines))

	ri := 0
	lastNonBlankRendered := 0
	for si, srcLine := range srcLines {
		srcKey := normalizeForAlign(stripMarkdownTokens(srcLine))

		if srcKey == "" {
			// Check if the current rendered row is blank
			if ri < len(renderedLines) {
				rPlain := normalizeForAlign(ansiRE.ReplaceAllString(renderedLines[ri], ""))
				if rPlain == "" {
					// Current rendered row is blank: map to it and advance
					out[si] = ri
					ri++
					lastNonBlankRendered = ri - 1
				} else {
					// Current rendered row is not blank: map to last non-blank
					out[si] = lastNonBlankRendered
				}
			} else {
				// Past the end of rendered: map to last non-blank
				out[si] = lastNonBlankRendered
			}
			continue
		}

		found := -1
		for r := ri; r < len(renderedLines); r++ {
			rPlain := normalizeForAlign(ansiRE.ReplaceAllString(renderedLines[r], ""))
			if rPlain == "" {
				continue
			}
			if strings.Contains(rPlain, srcKey) || strings.Contains(srcKey, rPlain) {
				found = r
				break
			}
		}
		if found < 0 {
			if ri >= len(renderedLines) {
				found = len(renderedLines) - 1
				if found < 0 {
					found = 0
				}
			} else {
				found = ri
			}
		}
		out[si] = found
		lastNonBlankRendered = found
		ri = found + 1
	}
	return out
}

// stripMarkdownTokens removes the most common inline-decoration characters
// so a source line's text can be matched against the rendered output, where
// Glamour has replaced styling tokens with ANSI escape codes (already
// stripped by the caller).
func stripMarkdownTokens(s string) string {
	// Remove leading heading hashes and spaces, and bullet/list markers.
	s = strings.TrimSpace(s)
	for strings.HasPrefix(s, "#") {
		s = s[1:]
	}
	// Strip ordered-list markers like "1. ", "10. ", etc.
	if i := indexOfDot(s); i > 0 {
		digits := s[:i]
		allDigit := true
		for _, r := range digits {
			if r < '0' || r > '9' {
				allDigit = false
				break
			}
		}
		if allDigit && i+2 <= len(s) && s[i:i+2] == ". " {
			s = s[i+2:]
		}
	}
	if strings.HasPrefix(s, "- ") || strings.HasPrefix(s, "* ") || strings.HasPrefix(s, "+ ") {
		s = s[2:]
	}
	s = strings.TrimSpace(s)
	// Remove inline emphasis/code/link decoration characters anywhere.
	repl := strings.NewReplacer(
		"**", "",
		"__", "",
		"*", "",
		"_", "",
		"`", "",
		"[", "",
		"]", "",
	)
	return repl.Replace(s)
}

// indexOfDot returns the index of the first '.' in s, or -1 if not found.
func indexOfDot(s string) int {
	for i, r := range s {
		if r == '.' {
			return i
		}
	}
	return -1
}

// normalizeRendered pads the rendered output with blank rows so that source
// line i always lives at rendered row i. Returns the padded output and the
// (now identity-ish) map suitable for direct lookup.
//
// If a source line wrapped to N rendered rows (long unbroken text on a
// narrow terminal), it stays as N rows and subsequent source lines shift
// down by N-1. The returned map reflects this: srcToRendered[i+1] =
// srcToRendered[i] + N. The identity property only holds when no line
// wraps; the contract is "source line i starts at rendered row map[i]".
func normalizeRendered(source, rendered string) (string, []int) {
	raw := buildSrcToRendered(source, rendered)
	renderedLines := strings.Split(rendered, "\n")

	out := make([]string, 0, len(renderedLines)+len(raw))
	newMap := make([]int, len(raw))

	rIdx := 0
	for si := 0; si < len(raw); si++ {
		target := raw[si]
		// Append rendered rows up to and including target.
		for rIdx <= target && rIdx < len(renderedLines) {
			out = append(out, renderedLines[rIdx])
			rIdx++
		}
		// If target collapsed onto a previous row (raw[si] == raw[si-1]),
		// we need to add a blank row to give this source line its own slot.
		if si > 0 && raw[si] == raw[si-1] {
			out = append(out, "")
		}
		newMap[si] = len(out) - 1
	}
	// Append any trailing rendered rows not covered above.
	for rIdx < len(renderedLines) {
		out = append(out, renderedLines[rIdx])
		rIdx++
	}
	return strings.Join(out, "\n"), newMap
}

// normalizeForAlign lowercases and collapses internal whitespace for a more
// resilient match. Returns "" for blank-or-whitespace input.
func normalizeForAlign(s string) string {
	s = strings.ToLower(strings.TrimSpace(s))
	if s == "" {
		return ""
	}
	return strings.Join(strings.Fields(s), " ")
}
