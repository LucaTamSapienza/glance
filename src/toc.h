#ifndef GLANCE_TOC_H
#define GLANCE_TOC_H

#include "render.h"

/* Table of contents, derived from a rendered Doc. The renderer tags each
 * heading's first line with its level, so the TOC is just those lines: no
 * separate source parse, and `line` indexes straight into the Doc for jumping. */

typedef struct {
    int   level;   /* 1..6 */
    int   line;    /* index into Doc.lines */
    char *title;   /* heading text, owned */
} TOCItem;

typedef struct { TOCItem *v; int n, cap; } TOC;

/* Build the TOC from d's tagged heading lines (cleared first). */
void toc_build(const Doc *d, TOC *out);

void toc_free(TOC *out);

#endif /* GLANCE_TOC_H */
