/* Unit tests for context.c — the pure budget planner. */
#include "../src/context.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
    /* Unlimited budget: everything picked, full granularity, nothing truncated. */
    {
        CtxCand c[] = {
            { 0, 10.0, 100, 20 },
            { 1,  5.0, 100, 20 },
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
            { 0, 10.0, 100, 20 },   /* note 0, best */
            { 0,  9.0, 100, 20 },   /* note 0, second */
            { 1,  5.0, 100, 20 },   /* note 1 */
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
        CtxCand c[] = { { 0, 10.0, 100, 10 } };
        CtxPlan p = context_plan(c, 1, 50);
        assert(p.npick == 1);
        assert(p.picks[0].granularity == CTX_ABSTRACT);
        assert(p.picks[0].tokens == 10);
        assert(p.ntrunc == 0);
        context_plan_free(&p);
    }

    /* Truncation: a candidate that fits at no granularity goes to the manifest. */
    {
        CtxCand c[] = { { 0, 10.0, 100, 100 } };
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

    printf("all context tests passed\n");
    return 0;
}
