/* toc_test.c — unit tests for table-of-contents extraction. */
#include "../src/toc.h"
#include "../src/render.h"

#include <stdio.h>
#include <string.h>

static int fails = 0;

static void expect(int cond, const char *msg) {
    if (!cond) { printf("FAIL: %s\n", msg); fails++; }
}

int main(void) {
    const char *src =
        "# Title\n\n"
        "some text\n\n"
        "## Section A\n\n"
        "more\n\n"
        "### Sub\n\n"
        "```\n# not a heading (code)\n```\n\n"
        "## Section B\n";
    Doc *d = render_doc(src, strlen(src), 80, 1);
    TOC t = {0};
    toc_build(d, &t);

    expect(t.n == 4, "four headings (code fence ignored)");
    if (t.n == 4) {
        expect(t.v[0].level == 1 && strcmp(t.v[0].title, "Title") == 0, "H1 Title");
        expect(t.v[1].level == 2 && strcmp(t.v[1].title, "Section A") == 0, "H2 Section A");
        expect(t.v[2].level == 3 && strcmp(t.v[2].title, "Sub") == 0, "H3 Sub");
        expect(t.v[3].level == 2 && strcmp(t.v[3].title, "Section B") == 0, "H2 Section B");
        /* lines must be ascending and index real heading lines */
        expect(t.v[0].line < t.v[1].line && t.v[1].line < t.v[2].line, "lines ascending");
        expect(d->lines[t.v[0].line].heading == 1, "line tagged as H1");
    }
    toc_free(&t);
    doc_free(d);

    /* A stray U+00A0 (NBSP) after the # run must be trimmed, like an ASCII space,
     * so same-level headings align in the panel. */
    const char *nb =
        "## \xC2\xA0NBSP Lead\n\n"
        "## Plain\n";
    Doc *d2 = render_doc(nb, strlen(nb), 80, 1);
    TOC t2 = {0};
    toc_build(d2, &t2);
    expect(t2.n == 2, "two NBSP-case headings");
    if (t2.n == 2) {
        expect(t2.v[0].level == 2 && strcmp(t2.v[0].title, "NBSP Lead") == 0,
               "leading NBSP trimmed from title");
        expect(strcmp(t2.v[1].title, "Plain") == 0, "plain title intact");
    }
    toc_free(&t2);
    doc_free(d2);

    if (fails) { printf("%d toc test(s) FAILED\n", fails); return 1; }
    printf("all toc tests passed\n");
    return 0;
}
