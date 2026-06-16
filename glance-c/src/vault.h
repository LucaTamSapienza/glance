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

/* Write the "stem" of a file or link name into out: drop any directory part and
 * a trailing ".md". Used to match a [[wikilink]] or relative path to a file. */
void vault_stem(const char *name, char *out, size_t cap);

/* A list of file paths relative to a vault root. */
typedef struct { char **v; int n, cap; } VFiles;

/* Recursively collect every *.md file under `root` (skipping dot-directories).
 * Paths are stored relative to `root`. `out` is cleared first. */
void vault_scan(const char *root, VFiles *out);

void vfiles_free(VFiles *out);

/* Find the vault root for `path`: walk up to the nearest ancestor containing a
 * `.git` or `.obsidian` entry; if none, use the file's own directory. The root
 * is written to `out`. */
void vault_root(const char *path, char *out, size_t cap);

/* Resolve a [[wikilink]] name to a file under `root` by stem (case-insensitive,
 * searching the whole tree). Returns an owned absolute-ish path (root/rel), or
 * NULL if no file matches. */
char *vault_find(const char *root, const char *name);

#endif /* GLANCE_VAULT_H */
