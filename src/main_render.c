/* main_render.c — slice-1 CLI: render a Markdown file (or stdin) to ANSI on
 * stdout. This is the C equivalent of `glance --render`, and proves the
 * renderer in isolation before any TUI exists.
 *
 *   glance-render [-w WIDTH] [-l] [--theme NAME] [file.md]
 *     -w WIDTH      wrap width (default: terminal width, or 80)
 *     -l            light theme (shortcut for --theme auto-light)
 *     --theme NAME  named colour theme (dracula, nord, …); default auto
 */
#include "render.h"
#include "doc_html.h"
#include "theme.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/ioctl.h>

/* Width of the controlling terminal, or 80 when it can't be determined. */
static int term_width(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        return ws.ws_col;
    return 80;
}

/* Remove "--theme NAME" from argv (returns NAME or NULL) so getopt sees only its
 * own short flags. */
static const char *pull_theme(int *argc, char **argv) {
    for (int i = 1; i + 1 < *argc; i++) {
        if (!strcmp(argv[i], "--theme")) {
            const char *val = argv[i + 1];
            for (int j = i; j + 2 < *argc; j++) argv[j] = argv[j + 2];
            *argc -= 2;
            return val;
        }
    }
    return NULL;
}

/* Remove a bare flag like "--html" from argv; returns 1 if it was present. */
static int pull_flag(int *argc, char **argv, const char *flag) {
    for (int i = 1; i < *argc; i++) {
        if (!strcmp(argv[i], flag)) {
            for (int j = i; j + 1 < *argc; j++) argv[j] = argv[j + 1];
            (*argc)--;
            return 1;
        }
    }
    return 0;
}

/* Parse flags, render the input Markdown, and print the ANSI to stdout. */
int main(int argc, char **argv) {
    const char *theme_name = pull_theme(&argc, argv);
    int as_html = pull_flag(&argc, argv, "--html");
    int width = 0, dark = 1, opt;
    while ((opt = getopt(argc, argv, "w:lh")) != -1) {
        switch (opt) {
            case 'w': width = atoi(optarg); break;
            case 'l': dark = 0; break;
            case 'h':
                printf("usage: %s [-w width] [-l] [--theme name] [--html] [file.md]\n"
                       "  -w WIDTH  wrap width (default: terminal width, or 80)\n"
                       "  -l        light theme (default: dark)\n"
                       "  --html    emit a self-contained themed HTML page (not ANSI)\n"
                       "  reads stdin when no file is given; writes to stdout.\n"
                       "  for the interactive TUI and full key list, run: glance --help\n",
                       argv[0]);
                return 0;
            default:
                fprintf(stderr, "usage: %s [-w width] [-l] [--theme name] [--html] [file.md]\n", argv[0]);
                return 2;
        }
    }
    if (width <= 0) width = term_width();
    const Theme *theme = theme_name ? theme_by_name(theme_name) : NULL;
    if (!theme) theme = theme_auto(dark);   /* -l → auto-light; unknown → auto */

    FILE *f = stdin;
    char dirbuf[4096]; const char *base = NULL;
    if (optind < argc) {
        f = fopen(argv[optind], "rb");
        if (!f) { perror(argv[optind]); return 1; }
        snprintf(dirbuf, sizeof dirbuf, "%s", argv[optind]);
        base = dirname(dirbuf);   /* resolve relative image paths for sizing */
    }

    size_t len = 0;
    char *src = read_file(f, &len);
    if (f != stdin) fclose(f);
    if (!src) { fprintf(stderr, "read failed\n"); return 1; }

    if (as_html) {
        char tbuf[256]; const char *title = "glance";
        if (optind < argc) {
            char nb[1024]; snprintf(nb, sizeof nb, "%s", argv[optind]);
            snprintf(tbuf, sizeof tbuf, "%s", basename(nb));
            title = tbuf;
        }
        char *html = md_to_html(src, len, theme, title);
        free(src);
        if (!html) { fprintf(stderr, "html render failed\n"); return 1; }
        fputs(html, stdout);
        free(html);
        return 0;
    }

    Doc *doc = render_doc_themed(src, len, width, theme, base);
    free(src);
    if (!doc) { fprintf(stderr, "render failed\n"); return 1; }

    char *out = doc_to_ansi(doc);
    doc_free(doc);
    if (!out) { fprintf(stderr, "serialize failed\n"); return 1; }

    fputs(out, stdout);
    free(out);
    return 0;
}
