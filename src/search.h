#ifndef GLANCE_SEARCH_H
#define GLANCE_SEARCH_H

#include "render.h"

/* Full-text search over a rendered Doc. Because the Doc stores plain run text
 * (not an ANSI string), matching needs no escape-stripping: we concatenate each
 * line's runs and scan. Matching is ASCII case-insensitive. Each Hit records a
 * line and the match's display-column span, ready for highlight overlay. */

typedef struct { int line, col, width; } Hit;

typedef struct { Hit *v; int n, cap; } Hits;

/* Find every occurrence of `query` in `d`, appending to `out` (cleared first).
 * An empty query yields no hits. */
void search_doc(const Doc *d, const char *query, Hits *out);

void hits_free(Hits *out);

#endif /* GLANCE_SEARCH_H */
