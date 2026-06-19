#ifndef GLANCE_EMBCACHE_H
#define GLANCE_EMBCACHE_H

#include <stddef.h>

/* embcache.c — a persistent, content-addressed embedding cache under a vault's
 * `.glance/` directory. Embedding a section with a real encoder costs real time
 * (see DESIGN.md §11); re-embedding the whole vault on every `--context` query
 * would be a non-starter. So section vectors are cached on disk keyed by a hash
 * of the section text: unchanged sections hit the cache, edited ones miss and
 * are re-embedded, and the query vector itself is always computed live.
 *
 * The cache is tagged with (dim, model_id). Opening it with a different
 * dimension or model starts empty, so switching the embedder never returns
 * stale vectors from another model. Pure (no llama dependency) and
 * unit-testable. */

typedef struct EmbCache EmbCache;

/* Open (or start) the cache for vault `dir`, expecting `dim`-float vectors from
 * the encoder identified by `model_id` (any short stable string, e.g.
 * "minilm-l6-384"). A cache file tagged with a different dim/model is ignored
 * (the next save overwrites it). Returns NULL only on OOM. */
EmbCache *embcache_open(const char *dir, int dim, const char *model_id);

/* The cached vector for `text` (dim floats, borrowed — valid until the next
 * put/free), or NULL on a miss. */
const float *embcache_get(EmbCache *c, const char *text, size_t len);

/* Insert `vec` (dim floats, copied) for `text`. A no-op if already present. */
void embcache_put(EmbCache *c, const char *text, size_t len, const float *vec);

/* Persist the cache to `<dir>/.glance/embeddings.bin` if it changed since open.
 * Creates `.glance/` if needed. Returns 0 on success (or if nothing to write). */
int embcache_save(EmbCache *c);

void embcache_free(EmbCache *c);

/* Lookup stats since open (for the token-receipt / diagnostics). */
int embcache_hits(const EmbCache *c);
int embcache_misses(const EmbCache *c);

#endif /* GLANCE_EMBCACHE_H */
