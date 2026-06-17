/* progress_test.c — unit tests for the Reader scroll/progress HUD logic. */
#include "../src/progress.h"

#include <stdio.h>
#include <string.h>

static int fails = 0;
static void expect(int got, int want, const char *msg) {
    if (got != want) { printf("FAIL: %s (got %d, want %d)\n", msg, got, want); fails++; }
}

int main(void) {
    /* percent at the boundaries and in between */
    expect(progress_percent(0, 100), 0,   "top is 0%");
    expect(progress_percent(99, 100), 100, "last line is 100%");
    expect(progress_percent(0, 1), 100,    "single line is 100%");
    expect(progress_percent(0, 0), 100,    "empty doc is 100%");
    expect(progress_percent(50, 101), 50,  "midpoint is 50%");
    expect(progress_percent(-5, 100), 0,   "negative line clamps to 0%");
    expect(progress_percent(500, 100), 100,"overflow clamps to 100%");

    /* ride-along: top and line move together */
    int top = 10, line = 12;
    progress_scroll(&top, &line, 3, 340, 20);
    expect(top, 13, "top advanced by 3");
    expect(line, 15, "line advanced by 3");

    /* clamp at the top edge */
    top = 0; line = 0;
    progress_scroll(&top, &line, -1, 340, 20);
    expect(top, 0, "top clamps at 0");
    expect(line, 0, "line clamps at 0");

    /* clamp at the bottom edge (maxtop = nline - body) */
    top = 330; line = 339;
    progress_scroll(&top, &line, 5, 340, 20);
    expect(top, 320, "top clamps at maxtop (320)");
    expect(line, 339, "line clamps at last line");

    /* short document: maxtop floors at 0, line still tracks */
    top = 0; line = 2;
    progress_scroll(&top, &line, 4, 5, 20);
    expect(top, 0, "short doc keeps top at 0");
    expect(line, 4, "line clamps at last (short doc)");

    /* spinner wraps mod 6 and is a single 3-byte glyph */
    expect(strcmp(progress_spinner(0), progress_spinner(6)), 0, "spinner wraps at 6");
    expect(strcmp(progress_spinner(1), progress_spinner(7)), 0, "spinner wraps (off by one)");
    expect(strcmp(progress_spinner(-1), progress_spinner(5)), 0, "negative frame wraps");
    expect((int)strlen(progress_spinner(0)), 3, "frame is 3 UTF-8 bytes");
    expect((int)strlen(PROGRESS_REST), 3, "rest glyph is 3 UTF-8 bytes");

    if (fails) { printf("%d progress test(s) FAILED\n", fails); return 1; }
    printf("all progress tests passed\n");
    return 0;
}
