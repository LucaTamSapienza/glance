/* fswatch_test.c — unit tests for the reload-decision logic. The kqueue plumbing
 * needs a real filesystem/terminal and is verified interactively; the pure
 * decision below is what governs cross-session live-sync behaviour. */
#include "../src/fswatch.h"

#include <stdio.h>

static int fails = 0;
static void expect(int cond, const char *msg) {
    if (!cond) { printf("FAIL: %s\n", msg); fails++; }
}

int main(void) {
    /* Identical content (e.g. our own atomic save echoing back) is a no-op. */
    expect(watch_reload_action(0, 0) == RELOAD_NONE, "same content, clean -> none");
    expect(watch_reload_action(0, 1) == RELOAD_NONE, "same content, dirty -> none");

    /* A real external change with no local edits: adopt it (live-sync). */
    expect(watch_reload_action(1, 0) == RELOAD_APPLY, "changed, clean -> apply");

    /* Both sides changed: don't clobber, surface a conflict prompt. */
    expect(watch_reload_action(1, 1) == RELOAD_CONFLICT, "changed, dirty -> conflict");

    if (fails) { printf("%d fswatch test(s) FAILED\n", fails); return 1; }
    printf("all fswatch tests passed\n");
    return 0;
}
