#ifndef GLANCE_EMBED_H
#define GLANCE_EMBED_H

#include <stddef.h>

/* embed.c — the embedding seam for semantic retrieval (behind `--semantic`).
 *
 * An Embedder maps text to a fixed-dimension, L2-normalized float vector;
 * `embed_cosine` compares two of them. Retrieval (`agent_context`) fuses an
 * embedding's cosine similarity with the lexical BM25 score so that notes a
 * keyword search would miss can still surface.
 *
 * The shipped default is dependency-free: feature-hashed token n-grams (the
 * "hashing trick"). It is deterministic and exercises the whole dense pipeline,
 * but it is a STRUCTURAL signal, not a semantic model — its similarity is
 * essentially weighted token overlap. The real quality jump comes from plugging
 * a small sentence encoder (MiniLM-class) in behind this exact interface; see
 * DESIGN.md §11. Lexical remains the default; semantic is opt-in. */

typedef struct Embedder Embedder;

/* Embed `len` bytes of UTF-8 `text` into the caller's `out` buffer, which must
 * hold embedder_dim(e) floats. The result is L2-normalized (zero vector for
 * empty text). */
typedef void (*EmbedFn)(const Embedder *e, const char *text, size_t len, float *out);

struct Embedder {
    int     dim;
    EmbedFn embed;
    void   *ctx;     /* model handle for a real encoder; NULL for the default */
};

/* The dependency-free default embedder at `dim` dimensions (e.g. 256). Owns no
 * external resources. Free with embedder_free. Returns NULL on bad dim/OOM. */
Embedder *embedder_default(int dim);

void embedder_free(Embedder *e);

static inline int embedder_dim(const Embedder *e) { return e ? e->dim : 0; }

/* Cosine similarity of two `dim`-length vectors, in [-1, 1] (0 if either is the
 * zero vector). */
float embed_cosine(const float *a, const float *b, int dim);

#endif /* GLANCE_EMBED_H */
