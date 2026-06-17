/* section.c — extract a heading's subtree from a rendered Doc, by anchor.
 * Matching reuses the TOC (tagged heading lines), so titles are already clean
 * of any heading-chip padding; the subtree ends at the next heading of the same
 * or higher level. */
#include "section.h"
#include "toc.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* ASCII-lowercase one byte. */
static char lc(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

/* Equal after trimming leading/trailing ASCII whitespace, ASCII case-folded. */
static int ci_trim_eq(const char *a, const char *b) {
    while (*a && isspace((unsigned char)*a)) a++;
    while (*b && isspace((unsigned char)*b)) b++;
    const char *ae = a + strlen(a), *be = b + strlen(b);
    while (ae > a && isspace((unsigned char)ae[-1])) ae--;
    while (be > b && isspace((unsigned char)be[-1])) be--;
    if ((ae - a) != (be - b)) return 0;
    for (; a < ae; a++, b++) if (lc(*a) != lc(*b)) return 0;
    return 1;
}

/* Write the GitHub-style slug of `s` into out[cap]: lowercased, ASCII
 * alphanumerics kept, runs of spaces/underscores/hyphens collapsed to a single
 * '-', everything else dropped, with no leading/trailing '-'. NUL-terminated. */
static void slug(const char *s, char *out, size_t cap) {
    size_t o = 0;
    int prev_dash = 1;   /* start true so a leading separator produces no '-' */
    for (; *s && o + 1 < cap; s++) {
        unsigned char c = (unsigned char)*s;
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) { out[o++] = (char)c; prev_dash = 0; }
        else if (c >= 'A' && c <= 'Z') { out[o++] = (char)(c - 'A' + 'a'); prev_dash = 0; }
        else if (c == ' ' || c == '-' || c == '_') { if (!prev_dash) { out[o++] = '-'; prev_dash = 1; } }
        /* else: punctuation / UTF-8 bytes are dropped */
    }
    while (o > 0 && out[o - 1] == '-') o--;
    out[o] = '\0';
}

/* Does heading `title` match `anchor`, by trimmed text or by slug? */
static int title_matches(const char *title, const char *anchor) {
    if (ci_trim_eq(title, anchor)) return 1;
    char s1[512], s2[512];
    slug(title, s1, sizeof s1);
    slug(anchor, s2, sizeof s2);
    return s1[0] && !strcmp(s1, s2);
}

Section section_find(const Doc *d, const char *anchor) {
    Section r = {0, 0, 0, 0};
    if (!anchor || !*anchor) {
        r.end = (int)d->nline;
        r.found = 1;
        return r;
    }
    TOC t = {0};
    toc_build(d, &t);
    int mi = -1;
    for (int i = 0; i < t.n; i++)
        if (title_matches(t.v[i].title, anchor)) { mi = i; break; }
    if (mi >= 0) {
        int level = t.v[mi].level;
        int end = (int)d->nline;
        for (int j = mi + 1; j < t.n; j++)
            if (t.v[j].level <= level) { end = t.v[j].line; break; }
        r.start = t.v[mi].line;
        r.end = end;
        r.level = level;
        r.found = 1;
    }
    toc_free(&t);
    return r;
}

char *section_text(const Doc *d, int start, int end) {
    if (start < 0) start = 0;
    if (end > (int)d->nline) end = (int)d->nline;
    size_t total = 1;   /* trailing NUL */
    for (int i = start; i < end; i++) {
        char *lt = line_text(&d->lines[i]);
        if (lt) { total += strlen(lt); free(lt); }
        total += 1;     /* one '\n' per line */
    }
    char *out = malloc(total);
    if (!out) return NULL;
    size_t o = 0;
    for (int i = start; i < end; i++) {
        char *lt = line_text(&d->lines[i]);
        if (lt) { size_t n = strlen(lt); memcpy(out + o, lt, n); o += n; free(lt); }
        out[o++] = '\n';
    }
    out[o] = '\0';
    return out;
}
