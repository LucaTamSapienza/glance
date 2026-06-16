/* vault_test.c — unit tests for link extraction. */
#include "../src/vault.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

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

    /* recursive scan + wikilink resolution over a temp directory tree */
    char dir[] = "/tmp/glance_vault_XXXXXX";
    if (mkdtemp(dir)) {
        char p[1024];
        snprintf(p, sizeof p, "%s/a.md", dir); fclose(fopen(p, "w"));
        snprintf(p, sizeof p, "%s/sub", dir);  mkdir(p, 0755);
        snprintf(p, sizeof p, "%s/sub/Deep Note.md", dir); fclose(fopen(p, "w"));

        VFiles f = {0};
        vault_scan(dir, &f);
        expect(f.n == 2, "scan finds two files across subdir");
        vfiles_free(&f);

        char *hit = vault_find(dir, "Deep Note");   /* by stem, in a subdir */
        expect(hit && strstr(hit, "sub/Deep Note.md") != NULL, "wikilink resolves into subdir");
        free(hit);
        char *miss = vault_find(dir, "nope");
        expect(miss == NULL, "missing wikilink resolves to NULL");
        free(miss);

        char stem[64]; vault_stem("x/y/Page.md", stem, sizeof stem);
        expect(strcmp(stem, "Page") == 0, "stem drops dir and .md");

        snprintf(p, sizeof p, "%s/sub/Deep Note.md", dir); unlink(p);
        snprintf(p, sizeof p, "%s/a.md", dir); unlink(p);
        snprintf(p, sizeof p, "%s/sub", dir); rmdir(p);
        rmdir(dir);
    }

    if (fails) { printf("%d vault test(s) FAILED\n", fails); return 1; }
    printf("all vault tests passed\n");
    return 0;
}
