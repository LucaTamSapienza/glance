/* vault.c — extract links from Markdown via md4c (Markdown links + wikilinks). */
#include "vault.h"

#include <md4c.h>
#include <stdlib.h>
#include <string.h>

/* Append a link (copying len bytes of target), growing the array as needed. */
static void vlinks_push(VLinks *l, const char *target, size_t len, int wiki) {
    if (l->n == l->cap) {
        int nc = l->cap ? l->cap * 2 : 16;
        VLink *p = realloc(l->v, nc * sizeof(VLink));
        if (!p) return;
        l->v = p; l->cap = nc;
    }
    char *t = malloc(len + 1);
    if (!t) return;
    memcpy(t, target, len); t[len] = '\0';
    l->v[l->n].target = t; l->v[l->n].wiki = wiki;
    l->n++;
}

/* md4c span callback: record the target of each link span. */
static int on_enter_span(MD_SPANTYPE type, void *detail, void *ud) {
    VLinks *l = ud;
    if (type == MD_SPAN_A) {
        MD_SPAN_A_DETAIL *d = detail;
        if (d->href.size) vlinks_push(l, d->href.text, d->href.size, 0);
    } else if (type == MD_SPAN_WIKILINK) {
        MD_SPAN_WIKILINK_DETAIL *d = detail;
        if (d->target.size) vlinks_push(l, d->target.text, d->target.size, 1);
    }
    return 0;
}

static int ignore_block(MD_BLOCKTYPE t, void *d, void *u) { (void)t; (void)d; (void)u; return 0; }
static int ignore_span(MD_SPANTYPE t, void *d, void *u)   { (void)t; (void)d; (void)u; return 0; }
static int ignore_text(MD_TEXTTYPE t, const MD_CHAR *x, MD_SIZE s, void *u) {
    (void)t; (void)x; (void)s; (void)u; return 0;
}

void vault_links(const char *src, size_t len, VLinks *out) {
    out->n = 0;
    MD_PARSER p;
    memset(&p, 0, sizeof p);
    p.flags = MD_DIALECT_GITHUB | MD_FLAG_WIKILINKS;
    p.enter_block = ignore_block;
    p.leave_block = ignore_block;
    p.enter_span  = on_enter_span;
    p.leave_span  = ignore_span;
    p.text        = ignore_text;
    md_parse(src, (MD_SIZE)len, &p, out);
}

void vlinks_free(VLinks *out) {
    for (int i = 0; i < out->n; i++) free(out->v[i].target);
    free(out->v);
    out->v = NULL; out->n = out->cap = 0;
}
