#ifndef GLANCE_FUZZY_H
#define GLANCE_FUZZY_H

/* fuzzy.h — subsequence fuzzy matching for the file switcher.
 *
 * A pattern matches a string when all of its characters appear in the string in
 * order (case-insensitive). The score rewards matches that are consecutive, at
 * word boundaries (start, or after / _ - . space), and early in the string, so
 * the most relevant files rank first. Pure and unit-tested; no TUI state. */

/* Return 1 if `pattern` fuzzy-matches `str`, else 0. On a match, `*score` (when
 * non-NULL) receives the quality score (higher is better). An empty/NULL pattern
 * matches everything with score 0. */
int fuzzy_match(const char *pattern, const char *str, int *score);

/* Rank `files[0..n)` against `pattern`, writing the indices of the matches into
 * `out` (caller-allocated, length >= n), best first, and returning the count.
 * An empty pattern keeps every file in its original order. Ties break toward the
 * lower original index, so the order is stable. */
int fuzzy_rank(const char *pattern, const char *const *files, int n, int *out);

#endif /* GLANCE_FUZZY_H */
