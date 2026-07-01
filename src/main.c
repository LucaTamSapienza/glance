/* main.c — glance TUI entry point.
 *
 *   glance [file.md]      open a file in the Reader/Insert TUI
 *   cat x.md | glance     piped stdin, no file: render to stdout (a rendered cat)
 *   glance --help         full usage and key bindings
 *   glance --keys         diagnostic: print raw key events (see tui_keyprobe)
 *   glance --outline FILE  JSON heading tree   (agent-facing)
 *   glance --links FILE    JSON outbound links (agent-facing)
 *   glance --graph DIR     JSON vault link graph
 */
#include "tui.h"
#include "agent.h"
#include "mcp.h"
#include "render.h"
#include "theme.h"
#include "export.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>

/* Print the full usage: invocation forms, subcommands, and every key binding,
 * so `glance --help` is the single place that documents the whole program. */
static void print_help(void) {
    fputs(
"glance — a terminal Markdown reader & editor.\n"
"\n"
"USAGE\n"
"  glance [FILE]            open FILE in the TUI (a missing path starts a new,\n"
"                           empty file, created on first save)\n"
"  cat FILE | glance        render the piped Markdown to stdout (like glance-render)\n"
"  glance --keys            diagnostic: print raw key events, Esc to quit\n"
"  glance --outline FILE    print the heading tree as JSON (--depth N, --abstract)\n"
"  glance --section F#H     print the section under heading H as JSON (+ token receipt)\n"
"  glance --context \"Q\" DIR  retrieve a token-cheap context bundle for query Q\n"
"                           over vault DIR as JSON (--budget N caps tokens;\n"
"                           --semantic fuses embedding similarity with the lexical score)\n"
"  glance --links FILE      print the file's outbound links as JSON\n"
"  glance --backlinks N DIR notes linking to N as JSON (--context adds the line)\n"
"  glance --neighbors N DIR link-graph neighbourhood of note N (--depth H hops)\n"
"  glance --since TS DIR    notes in DIR modified after Unix time TS, as JSON\n"
"  glance --graph DIR       print the vault's link graph as JSON\n"
"  glance --edit F OP H T   edit section H of file F (OP=append|insert|replace,\n"
"                           T=text), saved atomically; prints the new section\n"
"  glance --set-frontmatter F K V   set YAML frontmatter key K to value V in F\n"
"  glance --export F [OUT]   export F to HTML (or PDF if OUT ends in .pdf);\n"
"                           OUT defaults to F with a .html extension\n"
"  glance mcp               serve the agent-memory tools over MCP (stdio JSON-RPC)\n"
"  glance --theme NAME      open using colour theme NAME (see --list-themes)\n"
"  glance --list-themes     list the available colour themes\n"
"  glance --help            show this help\n"
"\n"
"  glance-render [-w WIDTH] [-l] [--theme NAME] [--html] [FILE]   render to stdout\n"
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
"  Ctrl-P               fuzzy file switcher (type to filter, Enter opens)\n"
"  T                    theme picker             ?            toggle the help legend\n"
"  Ctrl-S               save                     Ctrl-C       quit\n"
"  :w :wq :q :q!        write / quit (vi-style)\n"
"\n"
"KEYS — Insert / Split\n"
"  Esc                  back to reader           Ctrl-S       save\n"
"  Ctrl-V               paste a clipboard image into a <name>_media/ folder\n"
"  Alt/Ctrl + arrows    jump word left / right   Cmd + arrows / Ctrl-A / Ctrl-E  line start / end\n"
"\n"
"  If Option/Cmd + arrows type letters instead of moving, your terminal doesn't\n"
"  report those modifiers in legacy mode: set `keyboard = enhanced` in\n"
"  ~/.config/glance/config (kitty keyboard protocol; check with glance --keys).\n",
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

/* Load ~/.config/glance/config (if present) into the theme registry, so the
 * configured default and any custom themes are known. */
static void load_user_config(void) {
    const char *home = getenv("HOME");
    if (!home) return;
    char path[4096];
    snprintf(path, sizeof path, "%s/.config/glance/config", home);
    FILE *f = fopen(path, "rb");
    if (!f) return;
    size_t n; char *b = read_file(f, &n);
    fclose(f);
    if (b) { theme_load_config(b); free(b); }
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
        load_user_config();               /* so custom themes show too */
        for (int i = 0; i < theme_count(); i++) printf("%s\n", theme_at(i)->name);
        return 0;
    }
    if (argc > 1 && (!strcmp(argv[1], "-k") || !strcmp(argv[1], "--keys")))
        return tui_keyprobe();
    if (argc > 1 && !strcmp(argv[1], "mcp")) return mcp_serve();
    if (argc > 2 && !strcmp(argv[1], "--outline")) {
        /* glance --outline FILE [--depth N] [--abstract] */
        const char *fpath = argv[2];
        int depth = 0, abstract = 0;
        for (int i = 3; i < argc; i++) {
            if (!strcmp(argv[i], "--depth") && i + 1 < argc) depth = (int)strtol(argv[++i], NULL, 10);
            else if (!strcmp(argv[i], "--abstract")) abstract = 1;
        }
        size_t l; char *s = load(fpath, &l);
        if (!s) return 1;
        agent_outline_ex(s, l, depth, abstract);
        free(s);
        return 0;
    }
    if (argc > 3 && !strcmp(argv[1], "--neighbors")) {
        /* glance --neighbors "Note" DIR [--depth N] */
        const char *note = argv[2], *dir = argv[3];
        int depth = 1;
        for (int i = 4; i < argc; i++)
            if (!strcmp(argv[i], "--depth") && i + 1 < argc) depth = (int)strtol(argv[++i], NULL, 10);
        return agent_neighbors(dir, note, depth);
    }
    if (argc > 2 && !strcmp(argv[1], "--links"))   return run_export(agent_links, argv[2]);
    if (argc > 3 && !strcmp(argv[1], "--backlinks")) {
        /* glance --backlinks "Note" DIR [--context] */
        const char *note = argv[2], *dir = argv[3];
        int want_context = 0;
        for (int i = 4; i < argc; i++) if (!strcmp(argv[i], "--context")) want_context = 1;
        return agent_backlinks(dir, note, want_context);
    }
    if (argc > 3 && !strcmp(argv[1], "--since")) {
        /* glance --since TIMESTAMP DIR */
        long ts = strtol(argv[2], NULL, 10);
        return agent_since(argv[3], ts);
    }
    if (argc > 5 && !strcmp(argv[1], "--edit")) {
        /* glance --edit FILE OP "Heading" "text"  (OP = append|insert|replace) */
        const char *file = argv[2], *opname = argv[3], *anchor = argv[4], *text = argv[5];
        int op;
        if (!strcmp(opname, "append")) op = 0;
        else if (!strcmp(opname, "insert")) op = 1;
        else if (!strcmp(opname, "replace")) op = 2;
        else { fprintf(stderr, "glance --edit: OP must be append, insert, or replace\n"); return 2; }
        return agent_edit(file, anchor, op, text);
    }
    if (argc > 4 && !strcmp(argv[1], "--set-frontmatter")) {
        /* glance --set-frontmatter FILE KEY VALUE */
        return agent_frontmatter(argv[2], argv[3], argv[4]);
    }
    if (argc > 2 && !strcmp(argv[1], "--export")) {
        /* glance --export FILE [OUT]; OUT extension picks HTML/PDF, default .html */
        const char *in = argv[2];
        char outbuf[1100];
        const char *out = (argc > 3) ? argv[3] : NULL;
        if (!out) {                          /* default OUT = FILE with .html */
            char tmp[1024]; snprintf(tmp, sizeof tmp, "%s", in);
            char *dot = strrchr(tmp, '.'), *slash = strrchr(tmp, '/');
            if (dot && (!slash || dot > slash)) *dot = '\0';   /* drop the extension */
            snprintf(outbuf, sizeof outbuf, "%s.html", tmp);
            out = outbuf;
        }
        const Theme *th = theme_name ? theme_by_name(theme_name) : NULL;
        if (!th) th = theme_auto(1);
        return export_file(in, out, th);
    }
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
        /* glance --context "QUERY" [DIR] [--budget N] [--semantic]; DIR = ".". */
        const char *query = argv[2], *dir = ".";
        size_t budget = 0;
        int semantic = 0;
        for (int i = 3; i < argc; i++) {
            if (!strcmp(argv[i], "--budget") && i + 1 < argc) budget = (size_t)strtoul(argv[++i], NULL, 10);
            else if (!strcmp(argv[i], "--semantic")) semantic = 1;
            else dir = argv[i];
        }
        return agent_context(dir, query, budget, semantic);
    }

    /* Piped stdin with no file argument: act as a filter — render the document
     * to stdout, like glance-render — instead of opening the TUI. A pipe can't
     * drive the interactive UI, and `cat notes.md | glance` should read as a
     * rendered `cat`. A file argument still opens the TUI. */
    if (argc <= 1 && !isatty(STDIN_FILENO)) {
        size_t slen = 0;
        char *ssrc = read_file(stdin, &slen);
        if (!ssrc) { fprintf(stderr, "read failed\n"); return 1; }
        struct winsize ws;
        int width = (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
                        ? ws.ws_col : 80;
        load_user_config();                    /* honour the configured default theme */
        const char *want = theme_name ? theme_name : theme_default_name();
        const Theme *th = (want && strcmp(want, "auto")) ? theme_by_name(want) : NULL;
        if (!th) th = theme_auto(1);           /* a pipe can't report polarity: dark */
        Doc *doc = render_doc_themed(ssrc, slen, width, th, NULL);
        free(ssrc);
        if (!doc) { fprintf(stderr, "render failed\n"); return 1; }
        char *out = doc_to_ansi(doc);
        doc_free(doc);
        if (!out) { fprintf(stderr, "serialize failed\n"); return 1; }
        fputs(out, stdout);
        free(out);
        return 0;
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
