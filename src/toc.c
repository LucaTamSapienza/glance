/* toc.c — build a table of contents from a rendered Doc's heading lines. */
#include "toc.h"

#include <stdlib.h>

/* Append an item (taking ownership of title), growing as needed. */
static void toc_push(TOC *t, int level, int line, char *title) {
    if (t->n == t->cap) {
        int nc = t->cap ? t->cap * 2 : 16;
        TOCItem *p = realloc(t->v, nc * sizeof(TOCItem));
        if (!p) { free(title); return; }
        t->v = p; t->cap = nc;
    }
    t->v[t->n].level = level; t->v[t->n].line = line; t->v[t->n].title = title;
    t->n++;
}

void toc_build(const Doc *d, TOC *out) {
    out->n = 0;
    for (size_t i = 0; i < d->nline; i++) {
        if (d->lines[i].heading <= 0) continue;
        char *title = line_text(&d->lines[i]);
        if (title) toc_push(out, d->lines[i].heading, (int)i, title);
    }
}

void toc_free(TOC *out) {
    for (int i = 0; i < out->n; i++) free(out->v[i].title);
    free(out->v);
    out->v = NULL; out->n = out->cap = 0;
}
