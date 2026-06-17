/* legend_test.c — unit tests for the Reader sidebar layout logic. */
#include "../src/legend.h"

#include <stdio.h>
#include <string.h>

static int fails = 0;
static void expect(int got, int want, const char *msg) {
    if (got != want) { printf("FAIL: %s (got %d, want %d)\n", msg, got, want); fails++; }
}

int main(void) {
    /* width split: closed -> full width; open -> width minus the panel */
    expect(legend_content_cols(100, 0, LEGEND_W), 100, "closed keeps full width");
    expect(legend_content_cols(100, 1, LEGEND_W), 70,  "open subtracts panel");
    expect(legend_content_cols(20,  1, LEGEND_W), 0,   "never negative");

    /* fallback: too narrow to reflow */
    expect(legend_should_overlay(100, LEGEND_W, LEGEND_MIN_CONTENT), 0, "wide: reflow");
    expect(legend_should_overlay(LEGEND_W + LEGEND_MIN_CONTENT, LEGEND_W,
                                 LEGEND_MIN_CONTENT), 0, "exactly fits: reflow");
    expect(legend_should_overlay(LEGEND_W + LEGEND_MIN_CONTENT - 1, LEGEND_W,
                                 LEGEND_MIN_CONTENT), 1, "one short: overlay");

    /* row formatting: every row is padded to exactly `inner` columns */
    char buf[64];
    int inner = LEGEND_W - 2;   /* 28 */

    LegendRow key = { "j k", "move line" };
    int n = legend_format_row(buf, sizeof buf, &key, inner, LEGEND_KEYCOL);
    expect(n, inner, "key row width");
    expect((int)strlen(buf), inner, "key row strlen == inner");
    expect(buf[0], ' ', "key row left pad");
    /* key at column 1; action one column past the keycol field (1+keycol+1) */
    expect(strncmp(buf + 1, "j k", 3), 0, "key at column 1");
    expect(strncmp(buf + 1 + LEGEND_KEYCOL + 1, "move line", 9), 0, "action after key field");

    LegendRow hdr = { NULL, "Navigate" };
    legend_format_row(buf, sizeof buf, &hdr, inner, LEGEND_KEYCOL);
    expect((int)strlen(buf), inner, "header strlen == inner");
    expect(buf[0], ' ', "header left pad");
    expect(strncmp(buf + 1, "Navigate", 8), 0, "header text at column 1");

    LegendRow spacer = { NULL, "" };
    legend_format_row(buf, sizeof buf, &spacer, inner, LEGEND_KEYCOL);
    expect((int)strlen(buf), inner, "spacer strlen == inner");
    int allspace = 1;
    for (int i = 0; i < inner; i++) if (buf[i] != ' ') allspace = 0;
    expect(allspace, 1, "spacer all spaces");

    /* truncation: an over-long action is clipped, never overruns inner */
    LegendRow longrow = { "x", "this action is far too long to fit the panel" };
    n = legend_format_row(buf, sizeof buf, &longrow, inner, LEGEND_KEYCOL);
    expect(n, inner, "long row clipped to inner");
    expect((int)strlen(buf), inner, "long row strlen == inner");

    /* an over-long key is clipped to the key column, not into the action */
    LegendRow longkey = { "abcdefghijklmno", "act" };
    legend_format_row(buf, sizeof buf, &longkey, inner, LEGEND_KEYCOL);
    expect((int)strlen(buf), inner, "long key strlen == inner");
    expect(strncmp(buf, " abcdefgh", 9), 0, "key truncated to keycol");

    /* every shipped Reader row fits without truncation */
    for (int i = 0; i < LEGEND_READER_N; i++) {
        const LegendRow *r = &LEGEND_READER[i];
        if (r->key && (int)strlen(r->key) > LEGEND_KEYCOL) {
            printf("FAIL: key \"%s\" exceeds key column\n", r->key); fails++;
        }
        int avail = inner - (1 + LEGEND_KEYCOL + 1);
        if (r->key && r->action && (int)strlen(r->action) > avail) {
            printf("FAIL: action \"%s\" exceeds action field\n", r->action); fails++;
        }
    }

    if (fails) { printf("%d legend test(s) FAILED\n", fails); return 1; }
    printf("all legend tests passed\n");
    return 0;
}
