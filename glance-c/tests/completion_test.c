/* completion_test.c — unit tests for bracket auto-pairing. */
#include "../src/completion.h"

#include <stdio.h>

static int fails = 0;
static void expect(int got, int want, const char *msg) {
    if (got != want) { printf("FAIL: %s (got %d, want %d)\n", msg, got, want); fails++; }
}

int main(void) {
    expect(pair_closer('['), ']', "[ -> ]");
    expect(pair_closer('('), ')', "( -> )");
    expect(pair_closer('{'), '}', "{ -> }");
    expect(pair_closer('`'), 0, "backtick not paired");
    expect(pair_closer('*'), 0, "asterisk not paired");
    expect(pair_closer('a'), 0, "letter not paired");

    expect(pair_should_skip(')', ')'), 1, "skip over closing paren");
    expect(pair_should_skip(']', ']'), 1, "skip over closing bracket");
    expect(pair_should_skip(')', 'x'), 0, "no skip when next differs");
    expect(pair_should_skip('(', '('), 0, "openers don't skip");

    if (fails) { printf("%d completion test(s) FAILED\n", fails); return 1; }
    printf("all completion tests passed\n");
    return 0;
}
