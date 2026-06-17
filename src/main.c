/* main.c — glance TUI entry point.
 *
 *   glance [file.md]      open a file (or stdin) in the Reader/Insert TUI
 *   glance --help         full usage and key bindings
 *   glance --keys         diagnostic: print raw key events (see tui_keyprobe)
 *   glance --outline FILE  JSON heading tree   (agent-facing)
 *   glance --links FILE    JSON outbound links (agent-facing)
 *   glance --graph DIR     JSON vault link graph
 */
#include "tui.h"
#include "agent.h"
#include "theme.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <errno.h>

/* Print the full usage: invocation forms, subcommands, and every key binding,
 * so `glance --help` is the single place that documents the whole program. */
static void print_help(void) {
    fputs(
"glance — a terminal Markdown reader & editor.\n"
"\n"
"USAGE\n"
"  glance [FILE]            open FILE in the TUI (a missing path starts a new,\n"
"                           empty file, created on first save)\n"
"  cat FILE | glance        read Markdown from stdin\n"
"  glance --keys            diagnostic: print raw key events, Esc to quit\n"
"  glance --outline FILE    print the heading tree as JSON (for agents)\n"
"  glance --section F#H     print the section under heading H as JSON (+ token receipt)\n"
"  glance --context \"Q\" DIR  retrieve a token-cheap context bundle for query Q\n"
"                           over vault DIR as JSON (--budget N caps the tokens)\n"
"  glance --links FILE      print the file's outbound links as JSON\n"
"  glance --graph DIR       print the vault's link graph as JSON\n"
"  glance --theme NAME      open using colour theme NAME (see --list-themes)\n"
"  glance --list-themes     list the available colour themes\n"
"  glance --help            show this help\n"
"\n"
"  glance-render [-w WIDTH] [-l] [--theme NAME] [FILE]   render to ANSI on stdout\n"
"\n"
"KEYS — Reader\n"
"  h j k l / arrows     move the cursor          g / G        top / bottom\n"
"  Ctrl-D / Ctrl-U      half page down / up      PgDn / PgUp  page down / up\n"
"  i                    insert mode (edit)       e            split: edit + preview\n"
"  t                    table of contents        / n N        search, next, prev\n"
"  v / V                select chars / lines      y            yank selection to clipboard\n"
"  Enter                open link / follow [[wikilink]] under the cursor\n"
"  - / Ctrl-O           back to the previous file (after following a link)\n"
"  b                    backlinks panel          Ctrl-G       graph explorer\n"
"  T                    theme picker             ?            toggle the help legend\n"
"  Ctrl-S               save                     Ctrl-C       quit\n"
"  :w :wq :q :q!        write / quit (vi-style)\n"
"\n"
"KEYS — Insert / Split\n"
"  Esc                  back to reader           Ctrl-S       save\n"
"  Ctrl-V               paste a clipboard image into a <name>_media/ folder\n"
"  Alt/Ctrl + arrows    jump word left / right   Cmd + arrows / Ctrl-A / Ctrl-E  line start / end\n",
        stdout);
}

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

/* Remove "--theme NAME" from argv if present and return NAME, else NULL, so the
 * rest of the argument handling stays positional. */
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

/* Load a file (or stdin) and run the TUI on it. The status-bar title is the
 * file's base name, or "stdin". Non-interactive subcommands print JSON & exit. */
int main(int argc, char **argv) {
    const char *theme_name = pull_theme(&argc, argv);
    if (argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
        print_help();
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--list-themes")) {
        const char *home = getenv("HOME");
        if (home) {                       /* load config so custom themes show too */
            char path[4096];
            snprintf(path, sizeof path, "%s/.config/glance/config", home);
            FILE *cf = fopen(path, "rb");
            if (cf) { size_t n; char *b = read_file(cf, &n); fclose(cf);
                      if (b) { theme_load_config(b); free(b); } }
        }
        for (int i = 0; i < theme_count(); i++) printf("%s\n", theme_at(i)->name);
        return 0;
    }
    if (argc > 1 && (!strcmp(argv[1], "-k") || !strcmp(argv[1], "--keys")))
        return tui_keyprobe();
    if (argc > 2 && !strcmp(argv[1], "--outline")) return run_export(agent_outline, argv[2]);
    if (argc > 2 && !strcmp(argv[1], "--links"))   return run_export(agent_links, argv[2]);
    if (argc > 2 && !strcmp(argv[1], "--graph"))   return agent_graph(argv[2]);
    if (argc > 2 && !strcmp(argv[1], "--section")) {
        /* Argument is "FILE#anchor"; a bare "FILE" selects the whole document. */
        char *arg = argv[2], *hash = strrchr(arg, '#');
        char fpath[1024];
        const char *anchor = NULL;
        if (hash) {
            size_t flen = (size_t)(hash - arg);
            if (flen >= sizeof fpath) flen = sizeof fpath - 1;
            memcpy(fpath, arg, flen); fpath[flen] = '\0';
            anchor = hash + 1;
        } else {
            snprintf(fpath, sizeof fpath, "%s", arg);
        }
        size_t l; char *s = load(fpath, &l);
        if (!s) return 1;
        agent_section(s, l, anchor);
        free(s);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "--context")) {
        /* glance --context "QUERY" [DIR] [--budget N]; DIR defaults to ".". */
        const char *query = argv[2], *dir = ".";
        size_t budget = 0;
        for (int i = 3; i < argc; i++) {
            if (!strcmp(argv[i], "--budget") && i + 1 < argc) budget = (size_t)strtoul(argv[++i], NULL, 10);
            else dir = argv[i];
        }
        return agent_context(dir, query, budget);
    }

    FILE *f = stdin;
    char title[256] = "stdin";
    const char *path = NULL;
    char *src = NULL;
    size_t len = 0;

    if (argc > 1) {
        path = argv[1];
        char tmp[1024];
        snprintf(tmp, sizeof tmp, "%s", argv[1]);
        snprintf(title, sizeof title, "%s", basename(tmp));
        f = fopen(argv[1], "rb");
        if (!f) {
            /* A missing path is not an error: open an empty buffer and keep the
             * name, so the first :w / Ctrl-S creates the file (like vim). Any
             * other failure (permissions, a directory) is still fatal. */
            if (errno != ENOENT) { perror(argv[1]); return 1; }
            src = calloc(1, 1);
        }
    }

    if (!src) {
        src = read_file(f, &len);
        if (f != stdin) fclose(f);
        if (!src) { fprintf(stderr, "read failed\n"); return 1; }
    }

    int rc = tui_run(src, len, path, title, theme_name);
    free(src);
    return rc;
}
