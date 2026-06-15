/* main.c — glance TUI entry point.
 *
 *   glance [file.md]   open a file (or stdin) in the Reader/Insert TUI
 *   glance --keys      diagnostic: print raw key events (see tui_keyprobe)
 */
#include "tui.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>

/* Load a file (or stdin) and run the TUI on it. The status-bar title is the
 * file's base name, or "stdin". */
int main(int argc, char **argv) {
    if (argc > 1 && (!strcmp(argv[1], "-k") || !strcmp(argv[1], "--keys")))
        return tui_keyprobe();

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

    int rc = tui_run(src, len, title);
    free(src);
    return rc;
}
