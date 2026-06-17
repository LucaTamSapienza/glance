/* Unit tests for embed.c — the default feature-hashing embedder. */
#include "../src/embed.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void embed(Embedder *e, const char *s, float *out) {
    e->embed(e, s, strlen(s), out);
}

int main(void) {
    const int D = 256;
    Embedder *e = embedder_default(D);
    assert(e && embedder_dim(e) == D);

    float a[256], b[256], c[256], z[256];

    /* Non-empty text is L2-normalized. */
    embed(e, "the quick brown fox jumps", a);
    double norm = 0;
    for (int i = 0; i < D; i++) norm += (double)a[i] * a[i];
    assert(fabs(sqrt(norm) - 1.0) < 1e-4);

    /* Identical text → cosine 1; deterministic. */
    embed(e, "the quick brown fox jumps", b);
    assert(fabs(embed_cosine(a, b, D) - 1.0) < 1e-5);

    /* Overlapping text is more similar than disjoint text. */
    embed(e, "the quick brown dog runs", b);        /* shares "the quick brown" */
    embed(e, "completely unrelated lexical content here", c);
    float sim_overlap = embed_cosine(a, b, D);
    float sim_disjoint = embed_cosine(a, c, D);
    assert(sim_overlap > sim_disjoint);
    assert(sim_overlap > 0.0f);

    /* Empty text → zero vector → cosine 0. */
    embed(e, "", z);
    for (int i = 0; i < D; i++) assert(z[i] == 0.0f);
    assert(embed_cosine(a, z, D) == 0.0f);

    embedder_free(e);
    printf("all embed tests passed\n");
    return 0;
}
