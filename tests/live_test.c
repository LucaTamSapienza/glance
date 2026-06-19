/* live_test.c — unit tests for the WYSIWYG (Live) projection. */
#include "../src/live.h"
#include "../src/render.h"

#include <stdio.h>
#include <string.h>

static int fails = 0;

static void expect(int cond, const char *msg) {
    if (!cond) { printf("FAIL: %s\n", msg); fails++; }
}

/* The kept lines plus the dropped (active) block must account for the whole Doc,
 * and no kept line may carry the active source line. */
static void check_partition(Doc *d, int active) {
    LiveView lv;
    expect(live_build(d, active, &lv) == 0, "live_build succeeds");

    /* every kept line keeps the Doc's visual order (pointers are ascending) */
    for (int i = 1; i < lv.nline; i++)
        expect(lv.lines[i - 1] < lv.lines[i], "kept lines stay in order");

    /* no kept line maps to the active source line */
    int cur = -1;
    for (int i = 0; i < lv.nline; i++) {
        if (lv.lines[i]->source_line > 0) cur = lv.lines[i]->source_line - 1;
        expect(cur != active, "active line excluded from kept lines");
    }

    /* active_at sits within the kept range */
    expect(lv.active_at >= 0 && lv.active_at <= lv.nline, "active_at in range");

    /* the lines before active_at all precede the active source line */
    cur = -1;
    for (int i = 0; i < lv.active_at; i++)
        if (lv.lines[i]->source_line > 0) cur = lv.lines[i]->source_line - 1;
    expect(cur < active || lv.active_at == 0, "lines before active_at precede it");

    live_free(&lv);
}

int main(void) {
    const char *src =
        "# Title\n\n"          /* line 1 */
        "para one\n\n"         /* line 3 */
        "## Section\n\n"       /* line 5 */
        "- item a\n"           /* line 7 */
        "- item b\n";          /* line 8 */
    Doc *d = render_doc(src, strlen(src), 80, 1);

    /* Editing the heading: its rendered line(s) drop out, the rest stay. */
    LiveView lv;
    live_build(d, 0, &lv);
    expect(lv.nline < (int)d->nline, "active block dropped from the heading");
    expect(lv.active_at == 0, "heading is the first source line");
    live_free(&lv);

    /* Partition invariants hold for several active lines, including a blank one. */
    check_partition(d, 0);   /* heading */
    check_partition(d, 1);   /* blank line (no rendered line of its own) */
    check_partition(d, 2);   /* paragraph */
    check_partition(d, 6);   /* a list item */

    /* An out-of-range active line keeps everything and appends at the end. */
    live_build(d, 999, &lv);
    expect(lv.nline == (int)d->nline, "out-of-range active keeps all lines");
    expect(lv.active_at == lv.nline, "out-of-range active appends at the end");
    live_free(&lv);

    doc_free(d);

    /* An empty Doc yields an empty view. */
    Doc *empty = render_doc("", 0, 80, 1);
    live_build(empty, 0, &lv);
    expect(lv.nline == 0 && lv.active_at == 0, "empty doc -> empty view");
    live_free(&lv);
    doc_free(empty);

    if (fails == 0) printf("live_test: all passed\n");
    return fails ? 1 : 0;
}
