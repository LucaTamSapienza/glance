/* bm25.h — Okapi BM25 full-text ranking index over a corpus of documents. */
#ifndef GLANCE_BM25_H
#define GLANCE_BM25_H

#include <stddef.h>

typedef struct Bm25 Bm25;   /* opaque index */

/* Create an empty index. Returns NULL on OOM. */
Bm25 *bm25_new(void);

/* Add a document with caller-chosen integer `id` and its UTF-8 `text`
   (`len` bytes). Text is tokenized internally (see bm25.c). Documents may be
   added in any order; ids need not be contiguous. Returns 0 on success,
   non-zero on OOM. */
int bm25_add(Bm25 *ix, int id, const char *text, size_t len);

/* Finalize after all documents are added: computes avg doc length and freezes
   the index for querying. Call once before bm25_search. */
void bm25_finalize(Bm25 *ix);

typedef struct { int id; double score; } Bm25Hit;

/* Score the corpus against `query` and write up to `max` highest-scoring docs
   (descending score, ties broken by ascending id) into `out`. Returns the
   number written. Documents with score <= 0 are omitted. An empty query
   returns 0. */
int bm25_search(const Bm25 *ix, const char *query, Bm25Hit *out, int max);

/* Release everything owned by the index. Safe on NULL. */
void bm25_free(Bm25 *ix);

#endif
