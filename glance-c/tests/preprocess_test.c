/* preprocess_test.c — unit tests for tolerant-Markdown fix-ups. */
#include "../src/preprocess.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0;

/* Assert preprocess(in) == want. */
static void check(const char *in, const char *want, const char *msg) {
    size_t n;
    char *got = preprocess(in, strlen(in), &n);
    if (strcmp(got, want) != 0) {
        printf("FAIL: %s\n  in  : %s\n  want: %s\n  got : %s\n", msg, in, want, got);
        fails++;
    }
    free(got);
}

static void test_bold_tighten(void) {
    check("** word **", "**word**", "tighten ** **");
    check("__ x __", "__x__", "tighten __ __");
    check("**word**", "**word**", "already tight unchanged");
    check("** **", "** **", "empty bold left alone");
    check("a **b** c", "a **b** c", "tight bold mid-line unchanged");
    check("** a ** and ** b **", "**a** and **b**", "two spans tightened");
    check("`** x **`", "`** x **`", "inline code preserved");
    check("```\n** x **\n```", "```\n** x **\n```", "fenced code preserved");
}

static void test_setext_neutralize(void) {
    check("text\n---", "text\n\n---", "dashes under text -> blank inserted");
    check("text\n===", "text\n\n===", "equals under text -> blank inserted");
    check("text\n-", "text\n\n-", "single dash under text neutralized");
    check("---", "---", "standalone rule unchanged");
    check("\n---", "\n---", "rule after blank unchanged");
    check("# Heading", "# Heading", "atx heading untouched");
}

static void test_mixed(void) {
    check("** a **\ntext\n---", "**a**\ntext\n\n---", "bold + setext together");
    check("", "", "empty input");
    check("plain line", "plain line", "plain text untouched");
}

int main(void) {
    test_bold_tighten();
    test_setext_neutralize();
    test_mixed();
    if (fails) { printf("%d preprocess test(s) FAILED\n", fails); return 1; }
    printf("all preprocess tests passed\n");
    return 0;
}
