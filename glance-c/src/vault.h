#ifndef GLANCE_VAULT_H
#define GLANCE_VAULT_H

#include <stddef.h>

/* Vault = a folder of Markdown files connected by links. This module extracts
 * the connections so both the navigator (follow links, backlinks) and the
 * agent-facing JSON exports can share one source of truth. Link extraction goes
 * straight through md4c (with wikilinks enabled), independent of the renderer. */

typedef struct {
    char *target;   /* href for a Markdown link, or the page name for [[wiki]] */
    int   wiki;     /* 1 if this came from a [[wikilink]], else 0 */
} VLink;

typedef struct { VLink *v; int n, cap; } VLinks;

/* Collect every link in `src` (Markdown links and [[wikilinks]]) into `out`
 * (cleared first). */
void vault_links(const char *src, size_t len, VLinks *out);

void vlinks_free(VLinks *out);

#endif /* GLANCE_VAULT_H */
