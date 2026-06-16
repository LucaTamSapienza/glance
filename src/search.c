/* search.c — case-insensitive full-text search over a rendered Doc. */
#include "search.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Append a hit, growing the array as needed. */
static void hits_push(Hits *h, int line, int col, int width) {
    if (h->n == h->cap) {
        int nc = h->cap ? h->cap * 2 : 16;
        Hit *p = realloc(h->v, nc * sizeof(Hit));
        if (!p) return;
        h->v = p; h->cap = nc;
    }
    h->v[h->n].line = line; h->v[h->n].col = col; h->v[h->n].width = width;
    h->n++;
}

/* ASCII case-insensitive search for needle in [hay, hay+n); returns the byte
 * offset of the next match at or after `from`, or -1. */
static long find_ci(const char *hay, size_t n, const char *needle, size_t m, size_t from) {
    if (m == 0 || m > n) return -1;
    for (size_t i = from; i + m <= n; i++) {
        size_t k = 0;
        while (k < m && tolower((unsigned char)hay[i + k]) == tolower((unsigned char)needle[k]))
            k++;
        if (k == m) return (long)i;
    }
    return -1;
}

void search_doc(const Doc *d, const char *query, Hits *out) {
    out->n = 0;
    size_t qlen = query ? strlen(query) : 0;
    if (qlen == 0) return;

    for (size_t li = 0; li < d->nline; li++) {
        char *buf = line_text(&d->lines[li]);   /* this line's plain text */
        if (!buf) return;
        size_t len = strlen(buf);
        for (long at = 0; (at = find_ci(buf, len, query, qlen, at)) >= 0; at += (long)qlen) {
            int col = u8_width(buf, (size_t)at);
            int width = u8_width(buf + at, qlen);
            hits_push(out, (int)li, col, width);
        }
        free(buf);
    }
}

void hits_free(Hits *out) { free(out->v); out->v = NULL; out->n = out->cap = 0; }
