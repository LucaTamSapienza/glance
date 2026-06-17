/* bm25_test.c — unit tests for the Okapi BM25 ranking index. */
#include "../src/bm25.h"

#include <stdio.h>
#include <string.h>

static int fails = 0;

/* Add a document whose text is a C string. */
static void add(Bm25 *ix, int id, const char *s) {
    bm25_add(ix, id, s, strlen(s));
}

static void check(int cond, const char *msg) {
    if (!cond) { printf("FAIL: %s\n", msg); fails++; }
}

/* True if id appears among the first n hits. */
static int has_id(const Bm25Hit *h, int n, int id) {
    for (int i = 0; i < n; i++) if (h[i].id == id) return 1;
    return 0;
}

int main(void) {
    /* --- tf / ranking: repetition wins, non-matching doc is omitted. --- */
    {
        Bm25 *ix = bm25_new();
        add(ix, 0, "the quick brown fox");
        add(ix, 1, "the lazy dog sleeps");
        add(ix, 2, "quick fox quick fox");
        bm25_finalize(ix);

        Bm25Hit h[8];
        int n = bm25_search(ix, "quick fox", h, 8);
        check(n == 2, "quick fox -> 2 docs (id1 has score 0)");
        check(n >= 1 && h[0].id == 2, "id2 ranks first (highest tf)");
        check(has_id(h, n, 0), "id0 present");
        check(!has_id(h, n, 1), "id1 absent (score 0)");
        bm25_free(ix);
    }

    /* --- idf: a rare term dominates a term present in every document. --- */
    {
        Bm25 *ix = bm25_new();
        /* "common" is in every doc; only id2 has the rare "zephyr". */
        add(ix, 0, "common common common common");
        add(ix, 1, "common common common common");
        add(ix, 2, "common zephyr");
        bm25_finalize(ix);

        Bm25Hit h[8];
        int n = bm25_search(ix, "common zephyr", h, 8);
        check(n == 3, "all three docs match common");
        check(h[0].id == 2, "rare-term doc (id2) wins ranking");
        bm25_free(ix);
    }

    /* --- empty / unknown queries and empty index. --- */
    {
        Bm25 *ix = bm25_new();
        add(ix, 0, "alpha beta gamma");
        bm25_finalize(ix);

        Bm25Hit h[8];
        check(bm25_search(ix, "", h, 8) == 0, "empty query -> 0");
        check(bm25_search(ix, "   ,.!", h, 8) == 0, "punctuation-only query -> 0");
        check(bm25_search(ix, "delta epsilon", h, 8) == 0, "unknown terms -> 0");
        bm25_free(ix);

        Bm25 *empty = bm25_new();
        bm25_finalize(empty);
        check(bm25_search(empty, "alpha", h, 8) == 0, "empty index -> 0");
        bm25_free(empty);
    }

    /* --- max truncation returns the single top scorer. --- */
    {
        Bm25 *ix = bm25_new();
        add(ix, 0, "fox");
        add(ix, 1, "fox fox fox");
        add(ix, 2, "fox fox");
        bm25_finalize(ix);

        Bm25Hit h[8];
        int n = bm25_search(ix, "fox", h, 1);
        check(n == 1, "max=1 returns exactly one hit");
        check(n == 1 && h[0].id == 1, "the one hit is the top scorer (id1)");
        bm25_free(ix);
    }

    /* --- ascending-id tie-break for equal scores. --- */
    {
        Bm25 *ix = bm25_new();
        add(ix, 5, "word");
        add(ix, 2, "word");
        bm25_finalize(ix);

        Bm25Hit h[8];
        int n = bm25_search(ix, "word", h, 8);
        check(n == 2 && h[0].id == 2 && h[1].id == 5, "ties broken by ascending id");
        bm25_free(ix);
    }

    /* --- tokenizer: case-insensitive, UTF-8/punctuation as separators. --- */
    {
        Bm25 *ix = bm25_new();
        add(ix, 0, "Hello,World!");          /* two terms, mixed case */
        add(ix, 1, "caf\xc3\xa9 hello");      /* UTF-8 byte splits "caf"/"" */
        bm25_finalize(ix);

        Bm25Hit h[8];
        int n = bm25_search(ix, "HELLO", h, 8);
        check(n == 2, "HELLO matches Hello and hello (case-insensitive)");
        check(bm25_search(ix, "world", h, 8) == 1, "world matched from Hello,World!");
        bm25_free(ix);
    }

    if (fails) { printf("%d bm25 test(s) FAILED\n", fails); return 1; }
    printf("all bm25 tests passed\n");
    return 0;
}
