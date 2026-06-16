/* main.c — glance TUI entry point.
 *
 *   glance [file.md]      open a file (or stdin) in the Reader/Insert TUI
 *   glance --keys         diagnostic: print raw key events (see tui_keyprobe)
 *   glance --outline FILE  JSON heading tree   (agent-facing)
 *   glance --links FILE    JSON outbound links (agent-facing)
 *   glance --graph DIR     JSON vault link graph
 */
#include "tui.h"
#include "agent.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>

/* Read a whole file into a buffer; prints an error and returns NULL on failure. */
static char *load(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    char *s = read_file(f, len);
    fclose(f);
    return s;
}

/* Run an agent-facing JSON export over a file. Returns the process exit code. */
static int run_export(void (*fn)(const char *, size_t), const char *path) {
    size_t len; char *s = load(path, &len);
    if (!s) return 1;
    fn(s, len);
    free(s);
    return 0;
}

/* Load a file (or stdin) and run the TUI on it. The status-bar title is the
 * file's base name, or "stdin". Non-interactive subcommands print JSON & exit. */
int main(int argc, char **argv) {
    if (argc > 1 && (!strcmp(argv[1], "-k") || !strcmp(argv[1], "--keys")))
        return tui_keyprobe();
    if (argc > 2 && !strcmp(argv[1], "--outline")) return run_export(agent_outline, argv[2]);
    if (argc > 2 && !strcmp(argv[1], "--links"))   return run_export(agent_links, argv[2]);
    if (argc > 2 && !strcmp(argv[1], "--graph"))   return agent_graph(argv[2]);

    FILE *f = stdin;
    char title[256] = "stdin";
    if (argc > 1) {
        f = fopen(argv[1], "rb");
        if (!f) { perror(argv[1]); return 1; }
        char tmp[1024];
        snprintf(tmp, sizeof tmp, "%s", argv[1]);
        snprintf(title, sizeof title, "%s", basename(tmp));
    }

    size_t len = 0;
    char *src = read_file(f, &len);
    if (f != stdin) fclose(f);
    if (!src) { fprintf(stderr, "read failed\n"); return 1; }

    int rc = tui_run(src, len, f == stdin ? NULL : argv[1], title);
    free(src);
    return rc;
}
