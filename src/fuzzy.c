/* fuzzy.c — subsequence fuzzy matching + ranking for the file switcher. */
#include "fuzzy.h"

#include <stdlib.h>

/* ASCII lower-case (the file switcher matches paths, which are ASCII-dominant;
 * non-ASCII bytes compare verbatim, which is fine for a subsequence test). */
static char lc(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

/* A word boundary is the string start or a separator before the match. */
static int is_sep(char c) {
    return c == '/' || c == ' ' || c == '_' || c == '-' || c == '.';
}

int fuzzy_match(const char *pattern, const char *str, int *score) {
    if (!pattern || !*pattern) { if (score) *score = 0; return 1; }
    if (!str) return 0;

    const char *p = pattern;
    int s = 0, i = 0, prev_match = -2;
    char prevc = '/';                 /* treat the start as a boundary */
    for (const char *c = str; *c; c++, i++) {
        if (lc(*c) == lc(*p)) {
            int bonus = 1;
            if (prev_match == i - 1) bonus += 4;     /* consecutive run */
            if (is_sep(prevc))       bonus += 6;     /* at a word boundary */
            if (i < 16)              bonus += (16 - i) / 4;  /* earlier is better */
            s += bonus;
            prev_match = i;
            if (!*++p) { if (score) *score = s; return 1; }  /* whole pattern matched */
        }
        prevc = *c;
    }
    return 0;                          /* ran out of string before pattern */
}

/* A (file index, score) pair, ordered by score desc then index asc. */
typedef struct { int idx, score; } Ranked;

static int cmp_ranked(const void *a, const void *b) {
    const Ranked *x = a, *y = b;
    if (x->score != y->score) return y->score - x->score;   /* higher score first */
    return x->idx - y->idx;                                 /* stable on ties */
}

int fuzzy_rank(const char *pattern, const char *const *files, int n, int *out) {
    if (n <= 0) return 0;
    if (!pattern || !*pattern) {                /* keep original order, all match */
        for (int i = 0; i < n; i++) out[i] = i;
        return n;
    }
    Ranked *r = malloc((size_t)n * sizeof *r);
    if (!r) {                                    /* degrade: unranked subset */
        int m = 0;
        for (int i = 0; i < n; i++) if (fuzzy_match(pattern, files[i], NULL)) out[m++] = i;
        return m;
    }
    int m = 0, sc;
    for (int i = 0; i < n; i++)
        if (fuzzy_match(pattern, files[i], &sc)) { r[m].idx = i; r[m].score = sc; m++; }
    qsort(r, (size_t)m, sizeof *r, cmp_ranked);
    for (int i = 0; i < m; i++) out[i] = r[i].idx;
    free(r);
    return m;
}
