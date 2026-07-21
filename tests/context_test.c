/* Unit tests for context.c — the pure budget planner. */
#include "../src/context.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
    /* Unlimited budget: everything picked, full granularity, nothing truncated. */
    {
        CtxCand c[] = {
            { 0, 10.0, 100, 20, 0 },
            { 1,  5.0, 100, 20, 0 },
        };
        CtxPlan p = context_plan(c, 2, 0);
        assert(p.npick == 2 && p.ntrunc == 0);
        assert(p.picks[0].granularity == CTX_SECTION);
        assert(p.used_tokens == 200);
        context_plan_free(&p);
    }

    /* Diversity: note 0 has two high-scoring sections, note 1 one lower. With a
       budget that fits two sections, the planner must represent note 1 before
       giving note 0 a second section — even though note 0's second scores higher
       than note 1's only one. */
    {
        CtxCand c[] = {
            { 0, 10.0, 100, 20, 0 },   /* note 0, best */
            { 0,  9.0, 100, 20, 0 },   /* note 0, second */
            { 1,  5.0, 100, 20, 0 },   /* note 1 */
        };
        CtxPlan p = context_plan(c, 3, 200);
        assert(p.npick == 2);
        /* picks must cover both notes (0 and 1), not note 0 twice */
        int has0 = 0, has1 = 0;
        for (int i = 0; i < p.npick; i++) {
            int note = c[p.picks[i].cand].note_id;
            has0 |= (note == 0);
            has1 |= (note == 1);
        }
        assert(has0 && has1);
        /* candidate 1 (note 0's second) is the one left out */
        assert(p.ntrunc == 1 && p.trunc[0] == 1);
        context_plan_free(&p);
    }

    /* Coarse-to-fine: a candidate whose full text overflows the budget is taken
       as its cheaper abstract when that fits. */
    {
        CtxCand c[] = { { 0, 10.0, 100, 10, 0 } };
        CtxPlan p = context_plan(c, 1, 50);
        assert(p.npick == 1);
        assert(p.picks[0].granularity == CTX_ABSTRACT);
        assert(p.picks[0].tokens == 10);
        assert(p.ntrunc == 0);
        context_plan_free(&p);
    }

    /* Truncation: a candidate that fits at no granularity goes to the manifest. */
    {
        CtxCand c[] = { { 0, 10.0, 100, 100, 0 } };
        CtxPlan p = context_plan(c, 1, 50);
        assert(p.npick == 0);
        assert(p.ntrunc == 1 && p.trunc[0] == 0);
        context_plan_free(&p);
    }

    /* Empty input. */
    {
        CtxPlan p = context_plan(NULL, 0, 100);
        assert(p.npick == 0 && p.ntrunc == 0);
        context_plan_free(&p);
    }

    /* Direct-first: a graph-surfaced candidate must not displace a direct hit.
       Note 0 has two direct sections (the second is the "answer"); note 1 is
       surfaced by expansion with a score between them. Budget fits only two.
       The old single-pool diversity pass took {A1, B}; the invariant demands
       {A1, A2} with B in the manifest. */
    {
        CtxCand c[] = {
            { 0, 10.0, 100, 100, 0 },   /* A1: direct */
            { 0,  5.0, 100, 100, 0 },   /* A2: direct, the answer */
            { 1,  8.0, 100, 100, 1 },   /* B: graph-surfaced */
        };
        CtxPlan p = context_plan(c, 3, 200);
        assert(p.npick == 2);
        assert(p.picks[0].cand == 0 && p.picks[1].cand == 1);
        assert(p.ntrunc == 1 && p.trunc[0] == 2);
        context_plan_free(&p);
    }

    /* Direct-first is a bonus, not a ban: with budget for all three, the
       surfaced neighbour still enters — after the direct hits. */
    {
        CtxCand c[] = {
            { 0, 10.0, 100, 100, 0 },
            { 0,  5.0, 100, 100, 0 },
            { 1,  8.0, 100, 100, 1 },
        };
        CtxPlan p = context_plan(c, 3, 300);
        assert(p.npick == 3);
        assert(p.picks[2].cand == 2);           /* surfaced comes last */
        assert(p.ntrunc == 0);
        context_plan_free(&p);
        CtxPlan u = context_plan(c, 3, 0);      /* unlimited keeps it too */
        assert(u.npick == 3);
        context_plan_free(&u);
    }

    printf("all context tests passed\n");
    return 0;
}
