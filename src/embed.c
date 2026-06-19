/* embed.c — the default feature-hashing embedder + cosine (see embed.h). */
#include "embed.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* True for the bytes that make up a term (same rule as bm25.c). */
static int is_term_byte(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

static unsigned char lower(unsigned char c) {
    return (c >= 'A' && c <= 'Z') ? (unsigned char)(c - 'A' + 'a') : c;
}

/* FNV-1a hash of a byte span. */
static unsigned long fnv(const char *s, size_t n) {
    unsigned long h = 1469598103934665603UL;
    for (size_t i = 0; i < n; i++) { h ^= (unsigned char)s[i]; h *= 1099511628211UL; }
    return h;
}

/* Add a hashed feature into the accumulator: bucket = hash mod dim, signed by a
 * hash bit so collisions cancel rather than always add (the signed hashing
 * trick). */
static void add_feature(float *out, int dim, const char *tok, size_t n) {
    unsigned long h = fnv(tok, n);
    int bucket = (int)(h % (unsigned long)dim);
    float sign = (h & 0x100000000UL) ? -1.0f : 1.0f;
    out[bucket] += sign;
}

/* The default embedder: hash token unigrams and adjacent bigrams into `dim`
 * dimensions, then L2-normalize. */
static void default_embed(const Embedder *e, const char *text, size_t len, float *out) {
    int dim = e->dim;
    memset(out, 0, (size_t)dim * sizeof *out);

    char prev[64]; size_t prevn = 0;     /* previous token, for bigrams */
    char cur[64];
    size_t i = 0;
    while (i < len) {
        while (i < len && !is_term_byte((unsigned char)text[i])) i++;
        if (i >= len) break;
        size_t n = 0;
        while (i < len && is_term_byte((unsigned char)text[i])) {
            if (n < sizeof cur) cur[n] = (char)lower((unsigned char)text[i]);
            n++; i++;
        }
        size_t cn = n < sizeof cur ? n : sizeof cur;
        add_feature(out, dim, cur, cn);
        if (prevn) {                      /* bigram "prev cur" */
            char bg[130];
            size_t bn = 0;
            memcpy(bg, prev, prevn); bn = prevn;
            bg[bn++] = ' ';
            memcpy(bg + bn, cur, cn); bn += cn;
            add_feature(out, dim, bg, bn);
        }
        memcpy(prev, cur, cn); prevn = cn;
    }

    double norm = 0.0;
    for (int d = 0; d < dim; d++) norm += (double)out[d] * out[d];
    norm = sqrt(norm);
    if (norm > 0.0) for (int d = 0; d < dim; d++) out[d] = (float)(out[d] / norm);
}

Embedder *embedder_default(int dim) {
    if (dim <= 0) return NULL;
    Embedder *e = calloc(1, sizeof *e);
    if (!e) return NULL;
    e->dim = dim;
    e->embed = default_embed;
    e->ctx = NULL;
    return e;
}

void embedder_free(Embedder *e) {
    if (!e) return;
    if (e->free_ctx) e->free_ctx(e->ctx);   /* tear down a real encoder's model/context */
    free(e);
}

float embed_cosine(const float *a, const float *b, int dim) {
    double dot = 0.0, na = 0.0, nb = 0.0;
    for (int d = 0; d < dim; d++) {
        dot += (double)a[d] * b[d];
        na  += (double)a[d] * a[d];
        nb  += (double)b[d] * b[d];
    }
    if (na == 0.0 || nb == 0.0) return 0.0f;
    return (float)(dot / (sqrt(na) * sqrt(nb)));
}
