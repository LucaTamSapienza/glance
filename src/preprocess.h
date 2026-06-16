#ifndef GLANCE_PREPROCESS_H
#define GLANCE_PREPROCESS_H

#include <stddef.h>

/* "Tolerant Markdown" — small, opinionated fix-ups applied to the source before
 * it reaches the parser, ported from the Go app's preprocess.go:
 *
 *   - tighten bold delimiters: "** word **" renders bold like "**word**"
 *     (CommonMark, and md4c, require the delimiter to hug the text);
 *   - neutralize stray setext: a lone "---"/"===" line under a paragraph stays
 *     a rule/paragraph instead of silently turning the text above into a
 *     heading. glance prefers explicit ATX (#) headings.
 *
 * Inline code spans and fenced code blocks are preserved verbatim. Returns a
 * newly malloc'd, NUL-terminated buffer (caller frees); *out_len gets its
 * length. Returns NULL on allocation failure.
 */
char *preprocess(const char *src, size_t len, size_t *out_len);

#endif /* GLANCE_PREPROCESS_H */
