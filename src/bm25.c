/* bm25.c — Okapi BM25 ranking index.
 *
 * Scoring: for each query term, a document's contribution is
 *   idf(t) * (tf * (k1+1)) / (tf + k1 * (1 - b + b * dl/avgdl))
 * with k1 = 1.2, b = 0.75 and
 *   idf(t) = ln(1 + (N - df + 0.5) / (df + 0.5)).
 * A document's score is the sum of its per-term contributions.
 *
 * Tokenization: ASCII A–Z is lowercased to a–z; a "term" is a maximal run of
 * ASCII alphanumeric bytes [a-z0-9]; every other byte (punctuation, whitespace,
 * UTF-8 lead/continuation bytes) is a separator. Empty terms are skipped. There
 * is no stop-word removal — BM25's idf already down-weights common terms. */

#include "bm25.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#define BM25_K1 1.2
#define BM25_B  0.75

typedef struct { int doc; int tf; } Posting;   /* term frequency in one doc */

typedef struct {
    char *term;
    Posting *post;       /* one posting per containing doc (df == np) */
    int np, pcap;
} Term;

typedef struct { int id; int len; } DocEntry;  /* len in tokens */

struct Bm25 {
    DocEntry *docs; int nd, dcap;
    Term *terms; int nt, tcap;
    int *hash; int hcap;      /* open addressing: stores term index + 1, 0 = empty */
    char *scratch; size_t scap;
    double avgdl;
};

/* True for the bytes that make up a term. */
static int is_term_byte(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

/* Lowercase an ASCII byte (A–Z only). */
static unsigned char lower(unsigned char c) {
    return (c >= 'A' && c <= 'Z') ? (unsigned char)(c - 'A' + 'a') : c;
}

/* FNV-1a hash of a NUL-terminated string. */
static unsigned long hash_str(const char *s) {
    unsigned long h = 1469598103934665603UL;
    for (; *s; s++) { h ^= (unsigned char)*s; h *= 1099511628211UL; }
    return h;
}

Bm25 *bm25_new(void) {
    Bm25 *ix = calloc(1, sizeof *ix);
    if (!ix) return NULL;
    ix->hcap = 16;
    ix->hash = calloc((size_t)ix->hcap, sizeof(int));
    if (!ix->hash) { free(ix); return NULL; }
    return ix;
}

/* Append a document, returning its internal index, or -1 on OOM. */
static int doc_push(Bm25 *ix, int id) {
    if (ix->nd == ix->dcap) {
        int nc = ix->dcap ? ix->dcap * 2 : 16;
        DocEntry *p = realloc(ix->docs, (size_t)nc * sizeof(DocEntry));
        if (!p) return -1;
        ix->docs = p; ix->dcap = nc;
    }
    ix->docs[ix->nd].id = id;
    ix->docs[ix->nd].len = 0;
    return ix->nd++;
}

/* Grow the hash table, reinserting every existing term. Returns 0 on success. */
static int hash_grow(Bm25 *ix) {
    int nc = ix->hcap * 2;
    int *nh = calloc((size_t)nc, sizeof(int));
    if (!nh) return -1;
    unsigned long mask = (unsigned long)nc - 1;
    for (int i = 0; i < ix->nt; i++) {
        unsigned long p = hash_str(ix->terms[i].term) & mask;
        while (nh[p]) p = (p + 1) & mask;
        nh[p] = i + 1;
    }
    free(ix->hash);
    ix->hash = nh; ix->hcap = nc;
    return 0;
}

/* Find the slot for `str`; returns its term index, creating the term if absent.
   Returns -1 on OOM. */
static int term_intern(Bm25 *ix, const char *str) {
    if (ix->nt * 2 >= ix->hcap && hash_grow(ix) != 0) return -1;
    unsigned long mask = (unsigned long)ix->hcap - 1;
    unsigned long p = hash_str(str) & mask;
    while (ix->hash[p]) {
        int ti = ix->hash[p] - 1;
        if (strcmp(ix->terms[ti].term, str) == 0) return ti;
        p = (p + 1) & mask;
    }
    if (ix->nt == ix->tcap) {
        int nc = ix->tcap ? ix->tcap * 2 : 32;
        Term *t = realloc(ix->terms, (size_t)nc * sizeof(Term));
        if (!t) return -1;
        ix->terms = t; ix->tcap = nc;
    }
    Term *t = &ix->terms[ix->nt];
    t->term = strdup(str);
    if (!t->term) return -1;
    t->post = NULL; t->np = t->pcap = 0;
    int ti = ix->nt++;
    ix->hash[p] = ti + 1;
    return ti;
}

/* Look up an existing term, or -1 if absent. */
static int term_lookup(const Bm25 *ix, const char *str) {
    unsigned long mask = (unsigned long)ix->hcap - 1;
    unsigned long p = hash_str(str) & mask;
    while (ix->hash[p]) {
        int ti = ix->hash[p] - 1;
        if (strcmp(ix->terms[ti].term, str) == 0) return ti;
        p = (p + 1) & mask;
    }
    return -1;
}

/* Record one occurrence of term `ti` in doc `di`, coalescing repeats within the
   same doc into the term-frequency count. Returns 0 on success. */
static int term_hit(Bm25 *ix, int ti, int di) {
    Term *t = &ix->terms[ti];
    if (t->np > 0 && t->post[t->np - 1].doc == di) {
        t->post[t->np - 1].tf++;
        return 0;
    }
    if (t->np == t->pcap) {
        int nc = t->pcap ? t->pcap * 2 : 4;
        Posting *p = realloc(t->post, (size_t)nc * sizeof(Posting));
        if (!p) return -1;
        t->post = p; t->pcap = nc;
    }
    t->post[t->np].doc = di;
    t->post[t->np].tf = 1;
    t->np++;
    return 0;
}

/* Ensure the scratch token buffer holds at least `need` bytes. Returns 0 on ok. */
static int scratch_ensure(Bm25 *ix, size_t need) {
    if (need <= ix->scap) return 0;
    size_t nc = ix->scap ? ix->scap * 2 : 64;
    while (nc < need) nc *= 2;
    char *p = realloc(ix->scratch, nc);
    if (!p) return -1;
    ix->scratch = p; ix->scap = nc;
    return 0;
}

int bm25_add(Bm25 *ix, int id, const char *text, size_t len) {
    int di = doc_push(ix, id);
    if (di < 0) return 1;
    int dl = 0;
    size_t i = 0;
    while (i < len) {
        while (i < len && !is_term_byte((unsigned char)text[i])) i++;
        if (i >= len) break;
        size_t tl = 0;
        while (i < len && is_term_byte((unsigned char)text[i])) {
            if (scratch_ensure(ix, tl + 1) != 0) return 1;
            ix->scratch[tl++] = (char)lower((unsigned char)text[i]);
            i++;
        }
        if (scratch_ensure(ix, tl + 1) != 0) return 1;
        ix->scratch[tl] = '\0';
        int ti = term_intern(ix, ix->scratch);
        if (ti < 0) return 1;
        if (term_hit(ix, ti, di) != 0) return 1;
        dl++;
    }
    ix->docs[di].len = dl;
    return 0;
}

void bm25_finalize(Bm25 *ix) {
    if (!ix || ix->nd == 0) { if (ix) ix->avgdl = 0; return; }
    long total = 0;
    for (int i = 0; i < ix->nd; i++) total += ix->docs[i].len;
    ix->avgdl = (double)total / (double)ix->nd;
}

/* Order hits by descending score, breaking ties by ascending id. */
static int hit_cmp(const void *a, const void *b) {
    const Bm25Hit *x = a, *y = b;
    if (x->score < y->score) return 1;
    if (x->score > y->score) return -1;
    if (x->id < y->id) return -1;
    if (x->id > y->id) return 1;
    return 0;
}

int bm25_search(const Bm25 *ix, const char *query, Bm25Hit *out, int max) {
    if (!ix || ix->nd == 0 || ix->avgdl <= 0 || !query || max <= 0) return 0;

    double *score = calloc((size_t)ix->nd, sizeof(double));
    if (!score) return 0;

    /* Collect the distinct query terms that exist in the index. */
    int *qt = NULL; int nqt = 0, qcap = 0;
    char *buf = NULL; size_t bcap = 0;
    size_t i = 0, qlen = strlen(query);
    while (i < qlen) {
        while (i < qlen && !is_term_byte((unsigned char)query[i])) i++;
        if (i >= qlen) break;
        size_t tl = 0;
        while (i < qlen && is_term_byte((unsigned char)query[i])) {
            if (tl + 1 > bcap) {
                size_t nc = bcap ? bcap * 2 : 64;
                while (nc < tl + 1) nc *= 2;
                char *p = realloc(buf, nc);
                if (!p) { free(buf); free(qt); free(score); return 0; }
                buf = p; bcap = nc;
            }
            buf[tl++] = (char)lower((unsigned char)query[i]);
            i++;
        }
        buf[tl] = '\0';
        int ti = term_lookup(ix, buf);
        if (ti < 0) continue;
        int dup = 0;
        for (int k = 0; k < nqt; k++) if (qt[k] == ti) { dup = 1; break; }
        if (dup) continue;
        if (nqt == qcap) {
            int nc = qcap ? qcap * 2 : 8;
            int *p = realloc(qt, (size_t)nc * sizeof(int));
            if (!p) { free(buf); free(qt); free(score); return 0; }
            qt = p; qcap = nc;
        }
        qt[nqt++] = ti;
    }
    free(buf);

    double N = (double)ix->nd;
    for (int k = 0; k < nqt; k++) {
        Term *t = &ix->terms[qt[k]];
        double df = (double)t->np;
        double idf = log(1.0 + (N - df + 0.5) / (df + 0.5));
        for (int j = 0; j < t->np; j++) {
            int d = t->post[j].doc;
            double tf = (double)t->post[j].tf;
            double dl = (double)ix->docs[d].len;
            double denom = tf + BM25_K1 * (1.0 - BM25_B + BM25_B * dl / ix->avgdl);
            score[d] += idf * (tf * (BM25_K1 + 1.0)) / denom;
        }
    }
    free(qt);

    Bm25Hit *tmp = malloc((size_t)ix->nd * sizeof(Bm25Hit));
    if (!tmp) { free(score); return 0; }
    int n = 0;
    for (int d = 0; d < ix->nd; d++) {
        if (score[d] > 0) {
            tmp[n].id = ix->docs[d].id;
            tmp[n].score = score[d];
            n++;
        }
    }
    free(score);

    qsort(tmp, (size_t)n, sizeof(Bm25Hit), hit_cmp);
    if (n > max) n = max;
    for (int k = 0; k < n; k++) out[k] = tmp[k];
    free(tmp);
    return n;
}

void bm25_free(Bm25 *ix) {
    if (!ix) return;
    for (int i = 0; i < ix->nt; i++) {
        free(ix->terms[i].term);
        free(ix->terms[i].post);
    }
    free(ix->terms);
    free(ix->docs);
    free(ix->hash);
    free(ix->scratch);
    free(ix);
}
