/* main_render.c — slice-1 CLI: render a Markdown file (or stdin) to ANSI on
 * stdout. This is the C equivalent of `glance --render`, and proves the
 * renderer in isolation before any TUI exists.
 *
 *   glance-render [-w WIDTH] [-l] [file.md]
 *     -w WIDTH  wrap width (default: terminal width, or 80)
 *     -l        light theme (default: dark)
 */
#include "render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

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

static int term_width(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        return ws.ws_col;
    return 80;
}

int main(int argc, char **argv) {
    int width = 0, dark = 1, opt;
    while ((opt = getopt(argc, argv, "w:l")) != -1) {
        switch (opt) {
            case 'w': width = atoi(optarg); break;
            case 'l': dark = 0; break;
            default:
                fprintf(stderr, "usage: %s [-w width] [-l] [file.md]\n", argv[0]);
                return 2;
        }
    }
    if (width <= 0) width = term_width();

    FILE *f = stdin;
    if (optind < argc) {
        f = fopen(argv[optind], "rb");
        if (!f) { perror(argv[optind]); return 1; }
    }

    size_t len = 0;
    char *src = read_all(f, &len);
    if (f != stdin) fclose(f);
    if (!src) { fprintf(stderr, "read failed\n"); return 1; }

    Doc *doc = render_doc(src, len, width, dark);
    free(src);
    if (!doc) { fprintf(stderr, "render failed\n"); return 1; }

    char *out = doc_to_ansi(doc);
    doc_free(doc);
    if (!out) { fprintf(stderr, "serialize failed\n"); return 1; }

    fputs(out, stdout);
    free(out);
    return 0;
}
