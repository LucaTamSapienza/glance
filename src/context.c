/* context.c — pure budget planner for `glance --context` (see context.h). */
#include "context.h"

#include <stdlib.h>

/* Order candidate indices by score desc, breaking ties by index asc. Used with
 * qsort_r-free portability: the candidate array is passed through a file-static
 * pointer set just before the sort (single-threaded CLI path). */
static const CtxCand *g_cand;

static int by_score(const void *a, const void *b) {
    int ia = *(const int *)a, ib = *(const int *)b;
    double sa = g_cand[ia].score, sb = g_cand[ib].score;
    if (sa < sb) return 1;
    if (sa > sb) return -1;
    return ia - ib;   /* stable-ish: lower index first */
}

/* Token cost of taking candidate `i` whole, or 0 if it does not fit `remaining`
 * (budget 0 means unlimited). Writes the chosen granularity to *gran. Prefers
 * the full section; falls back to the abstract; returns 0 if neither fits. */
static size_t fit(const CtxCand *c, size_t remaining, int unlimited, int *gran) {
    if (unlimited || c->full_tokens <= remaining) { *gran = CTX_SECTION;  return c->full_tokens; }
    if (c->abstract_tokens <= remaining)          { *gran = CTX_ABSTRACT; return c->abstract_tokens; }
    return 0;
}

CtxPlan context_plan(const CtxCand *cand, int n, size_t budget) {
    CtxPlan p = {0};
    if (n <= 0) return p;

    int unlimited = (budget == 0);
    int *order = malloc((size_t)n * sizeof *order);
    char *taken = calloc((size_t)n, 1);
    /* note_seen is indexed by candidate position but keyed on note_id via a
     * linear check; vaults are small enough that this stays cheap. */
    p.picks = malloc((size_t)n * sizeof *p.picks);
    p.trunc = malloc((size_t)n * sizeof *p.trunc);
    if (!order || !taken || !p.picks || !p.trunc) {
        free(order); free(taken); free(p.picks); free(p.trunc);
        CtxPlan empty = {0};
        return empty;
    }
    for (int i = 0; i < n; i++) order[i] = i;
    g_cand = cand;
    qsort(order, (size_t)n, sizeof *order, by_score);

    size_t remaining = budget;

    /* Has any already-picked candidate the same note as `note_id`? */
    /* Pass 1 — diversity: at most one section per note, best score first. */
    for (int k = 0; k < n; k++) {
        int i = order[k];
        int dup = 0;
        for (int j = 0; j < p.npick; j++)
            if (cand[p.picks[j].cand].note_id == cand[i].note_id) { dup = 1; break; }
        if (dup) continue;
        int gran;
        size_t cost = fit(&cand[i], remaining, unlimited, &gran);
        if (cost == 0 && !unlimited) continue;   /* try again in pass 2 */
        p.picks[p.npick++] = (CtxPick){ i, gran, cost };
        taken[i] = 1;
        if (!unlimited) remaining -= cost;
    }

    /* Pass 2 — fill remaining budget with additional sections, score order. */
    for (int k = 0; k < n; k++) {
        int i = order[k];
        if (taken[i]) continue;
        int gran;
        size_t cost = fit(&cand[i], remaining, unlimited, &gran);
        if (cost == 0 && !unlimited) continue;
        p.picks[p.npick++] = (CtxPick){ i, gran, cost };
        taken[i] = 1;
        if (!unlimited) remaining -= cost;
    }

    /* Truncation manifest — everything not taken, in score order. */
    for (int k = 0; k < n; k++) {
        int i = order[k];
        if (!taken[i]) p.trunc[p.ntrunc++] = i;
    }

    for (int j = 0; j < p.npick; j++) p.used_tokens += p.picks[j].tokens;

    free(order);
    free(taken);
    return p;
}

void context_plan_free(CtxPlan *p) {
    if (!p) return;
    free(p->picks);
    free(p->trunc);
    p->picks = NULL;
    p->trunc = NULL;
    p->npick = p->ntrunc = 0;
}
