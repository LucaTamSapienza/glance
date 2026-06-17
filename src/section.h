#ifndef GLANCE_SECTION_H
#define GLANCE_SECTION_H

#include "render.h"

/* Section addressing: extract a single heading's subtree from a rendered Doc.
 * A "section" is a heading line plus everything beneath it, up to the next
 * heading of the same or higher level (or the end of the document). This is the
 * read primitive behind `glance --section "Note#Heading"`: an agent fetches just
 * the relevant subtree instead of the whole file. */

typedef struct {
    int start;   /* first Doc line of the section (the heading line) */
    int end;     /* one past the last Doc line of the section */
    int level;   /* heading level 1..6 of the matched heading (0 = whole doc) */
    int found;   /* 1 if a matching heading was found, else 0 */
} Section;

/* Locate the section whose heading matches `anchor`, matching either the
 * heading's exact text (trimmed, ASCII case-insensitive) or its GitHub-style
 * slug, so both "Some Heading" and "some-heading" resolve. A NULL or empty
 * anchor selects the whole document (start=0, end=nline, level=0, found=1).
 * On no match, found=0. */
Section section_find(const Doc *d, const char *anchor);

/* Concatenate the plain text of Doc lines [start,end), one line per '\n', into
 * a newly malloc'd NUL-terminated string (caller frees), or NULL on OOM. The
 * range is clamped to the document bounds. */
char *section_text(const Doc *d, int start, int end);

/* True if heading text `title` matches `anchor` — by trimmed, ASCII
 * case-insensitive text or by GitHub-style slug. Shared with the write API. */
int section_title_matches(const char *title, const char *anchor);

/* The "abstract" of a section [start,end): its heading line plus the first
 * non-empty paragraph beneath it (or just the heading if there is no body).
 * This is the cheap, low-resolution projection the budget planner falls back to
 * when the full section will not fit. Same ownership as section_text. */
char *section_abstract(const Doc *d, int start, int end);

#endif /* GLANCE_SECTION_H */
