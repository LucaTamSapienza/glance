/* export.c — export a Markdown file to HTML or PDF.
 *
 * HTML is produced in-process by the doc_html sink. PDF is best-effort: we
 * write the HTML to a temp file and hand it to the first HTML->PDF converter we
 * can find on the system, spawned with an argv vector (never a shell), so a
 * path with shell metacharacters cannot inject a command. */
#include "export.h"
#include "doc_html.h"
#include "util.h"
#include "fs_save.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>
#include <sys/wait.h>
#include <sys/stat.h>

int export_wants_pdf(const char *out) {
    if (!out) return 0;
    size_t n = strlen(out);
    return n >= 4 && strcasecmp(out + n - 4, ".pdf") == 0;
}

/* Spawn argv to completion (stdout/stderr to /dev/null). Returns the child's
 * exit code, or -1 if it could not be executed. */
static int run(char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        int dn = open("/dev/null", O_WRONLY);
        if (dn >= 0) { dup2(dn, 1); dup2(dn, 2); close(dn); }
        execvp(argv[0], argv);
        _exit(127);
    }
    int st;
    if (waitpid(pid, &st, 0) < 0) return -1;
    if (WIFEXITED(st)) return WEXITSTATUS(st);
    return -1;
}

/* Is `cmd` an executable we can run? Absolute/relative paths are checked
 * directly; a bare name is searched on PATH. */
static int have_cmd(const char *cmd) {
    if (strchr(cmd, '/')) return access(cmd, X_OK) == 0;
    const char *path = getenv("PATH");
    if (!path) return 0;
    char buf[4096];
    while (*path) {
        const char *colon = strchr(path, ':');
        size_t n = colon ? (size_t)(colon - path) : strlen(path);
        if (n && n + 1 + strlen(cmd) + 1 < sizeof buf) {
            memcpy(buf, path, n);
            buf[n] = '/';
            snprintf(buf + n + 1, sizeof buf - n - 1, "%s", cmd);
            if (access(buf, X_OK) == 0) return 1;
        }
        if (!colon) break;
        path = colon + 1;
    }
    return 0;
}

/* Convert the HTML at `html_path` to a PDF at `out`, trying known converters in
 * turn. Returns 0 on success, non-zero if none worked. */
static int html_to_pdf(const char *html_path, const char *out) {
    /* weasyprint <in.html> <out.pdf> */
    if (have_cmd("weasyprint")) {
        char *argv[] = { "weasyprint", (char *)html_path, (char *)out, NULL };
        if (run(argv) == 0) return 0;
    }
    /* wkhtmltopdf -q <in.html> <out.pdf> */
    if (have_cmd("wkhtmltopdf")) {
        char *argv[] = { "wkhtmltopdf", "-q", (char *)html_path, (char *)out, NULL };
        if (run(argv) == 0) return 0;
    }
    /* Headless Chrome / Chromium: --print-to-pdf=<out> <in.html> */
    static const char *chromes[] = {
        "google-chrome", "google-chrome-stable", "chromium", "chromium-browser",
        "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
        "/Applications/Chromium.app/Contents/MacOS/Chromium",
    };
    for (size_t i = 0; i < sizeof chromes / sizeof chromes[0]; i++) {
        if (!have_cmd(chromes[i])) continue;
        char pflag[4200];
        snprintf(pflag, sizeof pflag, "--print-to-pdf=%s", out);
        char *argv[] = { (char *)chromes[i], "--headless=new", "--disable-gpu",
                         "--no-pdf-header-footer", pflag, (char *)html_path, NULL };
        if (run(argv) == 0) return 0;
    }
    return 1;
}

int export_file(const char *in, const char *out, const Theme *theme) {
    FILE *f = fopen(in, "rb");
    if (!f) { perror(in); return 1; }
    size_t len; char *src = read_file(f, &len);
    fclose(f);
    if (!src) { fprintf(stderr, "%s: read failed\n", in); return 1; }

    char nb[1024]; snprintf(nb, sizeof nb, "%s", in);
    char *html = md_to_html(src, len, theme, basename(nb));
    free(src);
    if (!html) { fprintf(stderr, "html render failed\n"); return 1; }

    if (!export_wants_pdf(out)) {
        if (atomic_write(out, html, strlen(html)) != 0) {
            fprintf(stderr, "%s: write failed\n", out);
            free(html); return 1;
        }
        free(html);
        fprintf(stderr, "wrote %s\n", out);
        return 0;
    }

    /* PDF: stage the HTML next to the output, convert, then clean up. */
    char tmp[4200];
    snprintf(tmp, sizeof tmp, "%s.html.tmp", out);
    int wrote = atomic_write(tmp, html, strlen(html));
    free(html);
    if (wrote != 0) { fprintf(stderr, "%s: write failed\n", tmp); return 1; }

    int rc = html_to_pdf(tmp, out);
    unlink(tmp);
    if (rc != 0) {
        fprintf(stderr, "no HTML->PDF converter found. Install one of: weasyprint, "
                        "wkhtmltopdf, or Google Chrome / Chromium.\n"
                        "Tip: export to .html instead, or pipe glance-render --html.\n");
        return 1;
    }
    fprintf(stderr, "wrote %s\n", out);
    return 0;
}
