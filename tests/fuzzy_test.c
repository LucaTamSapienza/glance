/* fuzzy_test.c — unit tests for subsequence fuzzy matching + ranking. */
#include "../src/fuzzy.h"

#include <stdio.h>
#include <string.h>

static int fails = 0;
static void expect(int cond, const char *msg) {
    if (!cond) { printf("FAIL: %s\n", msg); fails++; }
}

int main(void) {
    int s;
    /* Basic subsequence matching, case-insensitive. */
    expect(fuzzy_match("fb", "foobar", &s) == 1, "fb matches foobar");
    expect(fuzzy_match("FB", "foobar", &s) == 1, "match is case-insensitive");
    expect(fuzzy_match("xyz", "foobar", &s) == 0, "non-subsequence rejected");
    expect(fuzzy_match("oob", "foobar", &s) == 1, "interior subsequence matches");
    expect(fuzzy_match("", "anything", &s) == 1 && s == 0, "empty pattern matches");

    /* Pattern longer than the candidate cannot match. */
    expect(fuzzy_match("foobarbaz", "foo", &s) == 0, "too-long pattern rejected");

    /* Scoring: a word-boundary / consecutive hit beats a scattered one. */
    int s_boundary, s_scattered;
    fuzzy_match("read", "render-doc-readme", &s_scattered);
    fuzzy_match("read", "readme", &s_boundary);
    expect(s_boundary > s_scattered, "boundary+consecutive scores higher");

    /* Ranking puts the better candidate first. */
    const char *files[] = { "notes/random.md", "readme.md", "src/thread.md" };
    int out[3];
    int m = fuzzy_rank("read", files, 3, out);
    expect(m == 2, "two of three match 'read'");
    expect(out[0] == 1, "readme.md ranks first");

    /* Empty pattern keeps all files in original order. */
    m = fuzzy_rank("", files, 3, out);
    expect(m == 3 && out[0] == 0 && out[2] == 2, "empty pattern: all, in order");

    /* A path-segment query matches across the '/'. */
    expect(fuzzy_match("srcth", "src/thread.md", &s) == 1, "matches across a slash");

    if (fails) { printf("%d fuzzy test(s) FAILED\n", fails); return 1; }
    printf("all fuzzy tests passed\n");
    return 0;
}
