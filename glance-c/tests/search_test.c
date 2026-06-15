/* search_test.c — unit tests for full-text search over a rendered Doc. */
#include "../src/search.h"
#include "../src/render.h"

#include <stdio.h>
#include <string.h>

static int fails = 0;

/* Render src, search for query, and assert the hit count. */
static int count(const char *src, const char *query) {
    Doc *d = render_doc(src, strlen(src), 80, 1);
    Hits h = {0};
    search_doc(d, query, &h);
    int n = h.n;
    hits_free(&h);
    doc_free(d);
    return n;
}

static void expect(int got, int want, const char *msg) {
    if (got != want) { printf("FAIL: %s (got %d, want %d)\n", msg, got, want); fails++; }
}

int main(void) {
    expect(count("hello Hello HELLO world", "hello"), 3, "case-insensitive, 3 matches");
    expect(count("hello Hello HELLO world", "world"), 1, "single match");
    expect(count("one two three", "xyz"), 0, "no match");
    expect(count("anything", ""), 0, "empty query");
    expect(count("# Heading\n\npara with word and word", "word"), 2, "across heading+para");

    /* hit columns: "ab xx cd" -> "xx" starts at display column 3 */
    Doc *d = render_doc("ab xx cd", 8, 80, 1);
    Hits h = {0};
    search_doc(d, "xx", &h);
    if (h.n != 1 || h.v[0].col != 3 || h.v[0].width != 2) {
        printf("FAIL: hit position (n=%d col=%d w=%d)\n", h.n, h.n ? h.v[0].col : -1,
               h.n ? h.v[0].width : -1);
        fails++;
    }
    hits_free(&h);
    doc_free(d);

    if (fails) { printf("%d search test(s) FAILED\n", fails); return 1; }
    printf("all search tests passed\n");
    return 0;
}
