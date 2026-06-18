/* toc.c — build a table of contents from a rendered Doc's heading lines. */
#include "toc.h"

#include <stdlib.h>
#include <string.h>

/* True if the bytes at p start a TOC pad space to trim: an ASCII space or a
 * U+00A0 non-breaking space (UTF-8 C2 A0). Markdown lets a stray NBSP follow the
 * `#` run (md4c keeps it as heading text); left in, it misaligns the panel. */
static int pad_len(const char *p) {
    if (*p == ' ') return 1;
    if ((unsigned char)p[0] == 0xC2 && (unsigned char)p[1] == 0xA0) return 2;
    return 0;
}

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
        if (!title) continue;
        size_t s = 0, e = strlen(title);     /* drop chip pad + stray NBSP spaces */
        for (int w; (w = pad_len(title + s)) > 0; ) s += (size_t)w;
        while (e > s) {
            if (title[e-1] == ' ') { e--; continue; }
            if (e - s >= 2 && (unsigned char)title[e-2] == 0xC2 &&
                (unsigned char)title[e-1] == 0xA0) { e -= 2; continue; }
            break;
        }
        if (s) memmove(title, title + s, e - s);
        title[e - s] = '\0';
        toc_push(out, d->lines[i].heading, (int)i, title);
    }
}

void toc_free(TOC *out) {
    for (int i = 0; i < out->n; i++) free(out->v[i].title);
    free(out->v);
    out->v = NULL; out->n = out->cap = 0;
}
