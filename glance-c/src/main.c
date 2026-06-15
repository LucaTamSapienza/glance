/* main.c — glance TUI entry point.
 *
 *   glance [file.md]      open a file (or stdin) in Reader mode
 *
 * Slice 2 ships Reader mode only; Insert/Split arrive in later slices.
 */
#include "tui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>

static char *read_all(FILE *f, size_t *out_len) {
    size_t cap = 1 << 16, len = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;
    size_t n;
    while ((n = fread(buf + len, 1, cap - len, f)) > 0) {
        len += n;
        if (len == cap) {
            cap *= 2;
            char *p = realloc(buf, cap);
            if (!p) { free(buf); return NULL; }
            buf = p;
        }
    }
    *out_len = len;
    return buf;
}

int main(int argc, char **argv) {
    const char *path = NULL;
    FILE *f = stdin;
    char title[256] = "stdin";

    if (argc > 1 && (strcmp(argv[1], "-k") == 0 || strcmp(argv[1], "--keys") == 0))
        return tui_keyprobe();

    if (argc > 1) {
        path = argv[1];
        f = fopen(path, "rb");
        if (!f) { perror(path); return 1; }
        char tmp[1024];
        snprintf(tmp, sizeof tmp, "%s", path);
        snprintf(title, sizeof title, "%s", basename(tmp));
    }

    size_t len = 0;
    char *src = read_all(f, &len);
    if (f != stdin) fclose(f);
    if (!src) { fprintf(stderr, "read failed\n"); return 1; }

    int rc = tui_run(src, len, title);
    free(src);
    return rc;
}
