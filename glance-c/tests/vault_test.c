/* vault_test.c — unit tests for link extraction. */
#include "../src/vault.h"

#include <stdio.h>
#include <string.h>

static int fails = 0;
static void expect(int cond, const char *msg) {
    if (!cond) { printf("FAIL: %s\n", msg); fails++; }
}

/* Does `out` contain a link with this target and wiki flag? */
static int has(VLinks *out, const char *target, int wiki) {
    for (int i = 0; i < out->n; i++)
        if (out->v[i].wiki == wiki && strcmp(out->v[i].target, target) == 0) return 1;
    return 0;
}

int main(void) {
    const char *src =
        "See [the spec](spec.md) and [home](https://x.io).\n\n"
        "Related: [[Design Notes]] and [[api]].\n\n"
        "`[[not a link in code]]` inline.\n\n"
        "```\n[[fenced]]\n```\n";
    VLinks out = {0};
    vault_links(src, strlen(src), &out);

    expect(has(&out, "spec.md", 0), "markdown link spec.md");
    expect(has(&out, "https://x.io", 0), "markdown link url");
    expect(has(&out, "Design Notes", 1), "wikilink Design Notes");
    expect(has(&out, "api", 1), "wikilink api");
    expect(!has(&out, "not a link in code", 1), "code span not a link");
    expect(!has(&out, "fenced", 1), "fenced code not a link");
    expect(out.n == 4, "exactly four links");

    vlinks_free(&out);

    VLinks empty = {0};
    vault_links("no links here", 13, &empty);
    expect(empty.n == 0, "no links");
    vlinks_free(&empty);

    if (fails) { printf("%d vault test(s) FAILED\n", fails); return 1; }
    printf("all vault tests passed\n");
    return 0;
}
