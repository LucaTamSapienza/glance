#ifndef GLANCE_CONTEXT_H
#define GLANCE_CONTEXT_H

#include <stddef.h>

/* context.c — the pure budget planner behind `glance --context`. Given scored
 * retrieval candidates (one per note section, already ranked by BM25 + a graph
 * prior), it decides which to include under a token budget. The decisions are:
 *
 *   - score order:    highest relevance first;
 *   - diversity:      a first pass takes at most one section per note so several
 *                     notes are represented before any note gets a second one;
 *   - coarse-to-fine: a candidate whose full section text does not fit is taken
 *                     as its cheaper abstract if that fits, else deferred;
 *   - truncation:     everything left out is reported (the manifest), so the
 *                     agent knows there is more and can follow up — never a
 *                     silent drop.
 *
 * All I/O (scanning the vault, rendering, scoring) lives in the caller; this
 * module is pure so it can be unit tested with synthetic candidates. */

typedef struct {
    int    note_id;          /* groups candidates from the same note */
    double score;            /* final relevance (BM25 + graph prior) */
    size_t full_tokens;      /* token cost of the full section text */
    size_t abstract_tokens;  /* token cost of the cheaper abstract projection */
} CtxCand;

enum { CTX_SECTION = 0, CTX_ABSTRACT = 1 };

typedef struct {
    int    cand;         /* index into the input candidate array */
    int    granularity;  /* CTX_SECTION (full) or CTX_ABSTRACT */
    size_t tokens;       /* tokens actually used for this pick */
} CtxPick;

typedef struct {
    CtxPick *picks;
    int      npick;
    int     *trunc;      /* candidate indices left out, in score order */
    int      ntrunc;
    size_t   used_tokens;/* sum of picks' tokens */
} CtxPlan;

/* Plan which of `n` candidates to include under `budget` tokens (0 = no cap).
 * Deterministic; ties broken by ascending candidate index. The returned arrays
 * are owned by the caller and freed with context_plan_free. */
CtxPlan context_plan(const CtxCand *cand, int n, size_t budget);

void context_plan_free(CtxPlan *p);

#endif /* GLANCE_CONTEXT_H */
