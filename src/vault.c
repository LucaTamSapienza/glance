/* vault.c — extract links from Markdown via md4c (Markdown links + wikilinks). */
#include "vault.h"

#include <md4c.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>

void vault_stem(const char *name, char *out, size_t cap) {
    const char *base = strrchr(name, '/');
    base = base ? base + 1 : name;
    snprintf(out, cap, "%s", base);
    size_t n = strlen(out);
    if (n > 3 && strcasecmp(out + n - 3, ".md") == 0) out[n - 3] = '\0';
}

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

/* No-op md4c callbacks: scanning for wikilinks only cares about span-enter
 * events, so block, span-leave, and text events are intentionally discarded. */
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

/* ---- recursive vault scan ------------------------------------------------- */

static void vfiles_push(VFiles *f, const char *rel) {
    if (f->n == f->cap) {
        int nc = f->cap ? f->cap * 2 : 32;
        char **p = realloc(f->v, nc * sizeof(char *));
        if (!p) return;
        f->v = p; f->cap = nc;
    }
    f->v[f->n++] = strdup(rel);
}

/* Recurse into root/rel, appending the .md files found (relative to root). */
static void scan_rec(const char *root, const char *rel, VFiles *out) {
    if (out->n > 100000) return;                 /* runaway guard */
    char dir[4096];
    if (rel[0]) snprintf(dir, sizeof dir, "%s/%s", root, rel);
    else        snprintf(dir, sizeof dir, "%s", root);
    DIR *dp = opendir(dir);
    if (!dp) return;
    struct dirent *e;
    while ((e = readdir(dp))) {
        if (e->d_name[0] == '.') continue;       /* skip ., .., and hidden */
        char childrel[4096], childpath[8192];
        if (rel[0]) snprintf(childrel, sizeof childrel, "%s/%s", rel, e->d_name);
        else        snprintf(childrel, sizeof childrel, "%s", e->d_name);
        snprintf(childpath, sizeof childpath, "%s/%s", root, childrel);
        struct stat st;
        if (stat(childpath, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            scan_rec(root, childrel, out);
        } else {
            size_t l = strlen(e->d_name);
            if (l > 3 && strcasecmp(e->d_name + l - 3, ".md") == 0)
                vfiles_push(out, childrel);
        }
    }
    closedir(dp);
}

void vault_scan(const char *root, VFiles *out) {
    out->n = 0;
    scan_rec(root, "", out);
}

void vfiles_free(VFiles *out) {
    for (int i = 0; i < out->n; i++) free(out->v[i]);
    free(out->v);
    out->v = NULL; out->n = out->cap = 0;
}

/* True if `dir` contains an entry named `name`. */
static int has_entry(const char *dir, const char *name) {
    char p[4096];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    struct stat st;
    return stat(p, &st) == 0;
}

void vault_root(const char *path, char *out, size_t cap) {
    /* start from the file's directory */
    char dir[4096];
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) *slash = '\0'; else snprintf(dir, sizeof dir, ".");

    /* walk up looking for a vault marker; remember the starting dir as fallback */
    char start[4096]; snprintf(start, sizeof start, "%s", dir);
    for (;;) {
        if (has_entry(dir, ".git") || has_entry(dir, ".obsidian")) {
            snprintf(out, cap, "%s", dir);
            return;
        }
        char *up = strrchr(dir, '/');
        if (!up || up == dir) break;             /* reached "/" or a bare name */
        *up = '\0';
    }
    snprintf(out, cap, "%s", start);             /* no marker: the file's dir */
}

char *vault_find(const char *root, const char *name) {
    char want[256]; vault_stem(name, want, sizeof want);
    VFiles f = {0};
    vault_scan(root, &f);
    char *hit = NULL;
    for (int i = 0; i < f.n && !hit; i++) {
        char have[256]; vault_stem(f.v[i], have, sizeof have);
        if (strcasecmp(have, want) == 0) {
            char full[8192];
            snprintf(full, sizeof full, "%s/%s", root, f.v[i]);
            hit = strdup(full);
        }
    }
    vfiles_free(&f);
    return hit;
}
