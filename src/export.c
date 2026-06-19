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
#include <signal.h>

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

/* Recursively remove a directory we created (a Chrome profile under /tmp). The
 * path is always our own mkdtemp result, never user input, so the argv `rm -rf`
 * is safe. */
static void rmrf(const char *dir) {
    char *argv[] = { "rm", "-rf", (char *)dir, NULL };
    run(argv);
}

/* True if `path` begins with the "%PDF" signature — i.e. a converter actually
 * produced a PDF, not an empty/garbage file. */
static int is_pdf(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char sig[4] = {0};
    size_t n = fread(sig, 1, 4, f);
    fclose(f);
    return n == 4 && memcmp(sig, "%PDF", 4) == 0;
}

/* Spawn argv (output to /dev/null) and wait up to `timeout_s` for the PDF at
 * `out` to appear. Returns 0 if `out` is a valid PDF afterwards. Polling rather
 * than a plain wait is deliberate: headless Chrome writes the PDF promptly but
 * can linger for tens of seconds before exiting, so we stop as soon as the file
 * is ready and kill a lingering child — and a genuinely stuck converter can't
 * hang glance forever. Converters that exit promptly (weasyprint/wkhtmltopdf)
 * are reaped on the first poll. */
static int run_to_pdf(char *const argv[], const char *out, int timeout_s) {
    /* Start from a clean slate: a stale PDF left by a previous export (or an
     * earlier converter in the fallthrough) would make the first is_pdf() poll
     * succeed before this converter has written anything, killing it and keeping
     * the old file. `out` is never the (separate) temp HTML, so this is safe. */
    unlink(out);
    pid_t pid = fork();
    if (pid < 0) return 1;
    if (pid == 0) {
        int dn = open("/dev/null", O_WRONLY);
        if (dn >= 0) { dup2(dn, 1); dup2(dn, 2); close(dn); }
        execvp(argv[0], argv);
        _exit(127);
    }
    int reaped = 0;
    for (int i = 0; i < timeout_s * 5; i++) {        /* poll every 200ms */
        if (waitpid(pid, NULL, WNOHANG) == pid) { reaped = 1; break; }
        if (is_pdf(out)) { usleep(400000); break; }  /* ready; let the write settle */
        usleep(200000);
    }
    if (!reaped) { kill(pid, SIGKILL); waitpid(pid, NULL, 0); }
    return is_pdf(out) ? 0 : 1;
}

/* Convert the HTML at `html_path` to a PDF at `out`, trying known converters in
 * turn. Returns 0 only when `out` is genuinely a PDF afterwards (validated by
 * run_to_pdf), so a converter that exits without producing one — e.g. headless
 * Chrome silently attaching to a running profile — is not mistaken for success. */
static int html_to_pdf(const char *html_path, const char *out) {
    /* weasyprint <in.html> <out.pdf> */
    if (have_cmd("weasyprint")) {
        char *argv[] = { "weasyprint", (char *)html_path, (char *)out, NULL };
        if (run_to_pdf(argv, out, 30) == 0) return 0;
    }
    /* wkhtmltopdf -q <in.html> <out.pdf> */
    if (have_cmd("wkhtmltopdf")) {
        char *argv[] = { "wkhtmltopdf", "-q", (char *)html_path, (char *)out, NULL };
        if (run_to_pdf(argv, out, 30) == 0) return 0;
    }
    /* Headless Chrome / Chromium: --print-to-pdf=<out> <in.html>. A dedicated,
     * throwaway --user-data-dir is essential: without it, when the user already
     * has Chrome open, the new process attaches to that profile and the headless
     * print silently no-ops (the cause behind "the PDF stayed HTML"). */
    static const char *chromes[] = {
        "google-chrome", "google-chrome-stable", "chromium", "chromium-browser",
        "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
        "/Applications/Chromium.app/Contents/MacOS/Chromium",
    };
    char profile[] = "/tmp/glance-chrome-XXXXXX";
    char *prof = mkdtemp(profile);
    int ok = 0;
    for (size_t i = 0; i < sizeof chromes / sizeof chromes[0] && !ok; i++) {
        if (!have_cmd(chromes[i])) continue;
        char pflag[4200], uflag[4200];
        snprintf(pflag, sizeof pflag, "--print-to-pdf=%s", out);
        snprintf(uflag, sizeof uflag, "--user-data-dir=%s", prof ? prof : "/tmp/glance-chrome");
        char *argv[] = { (char *)chromes[i], "--headless=new", "--disable-gpu",
                         "--no-first-run", "--no-default-browser-check",
                         "--no-pdf-header-footer", uflag, pflag, (char *)html_path, NULL };
        ok = (run_to_pdf(argv, out, 30) == 0);
    }
    if (prof) rmrf(prof);
    return ok ? 0 : 1;
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

    /* PDF: stage the HTML beside the output (a real .html extension — converters
     * key off it), convert, then clean up. */
    char tmp[4200];
    snprintf(tmp, sizeof tmp, "%s.glance-tmp.html", out);
    int wrote = atomic_write(tmp, html, strlen(html));
    if (wrote != 0) { free(html); fprintf(stderr, "%s: write failed\n", tmp); return 1; }

    int rc = html_to_pdf(tmp, out);
    unlink(tmp);
    if (rc != 0) {
        /* Fall back to a sibling .html so the export is never empty-handed. */
        char alt[4200];
        snprintf(alt, sizeof alt, "%s.html", out);
        int altok = atomic_write(alt, html, strlen(html)) == 0;
        free(html);
        fprintf(stderr, "could not produce a PDF (no working converter — install "
                        "weasyprint / wkhtmltopdf, or close other Chrome windows).\n");
        if (altok) fprintf(stderr, "wrote %s instead.\n", alt);
        else       fprintf(stderr, "%s: write failed too.\n", alt);
        return 1;
    }
    free(html);
    fprintf(stderr, "wrote %s\n", out);
    return 0;
}
