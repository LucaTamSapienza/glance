/* clipboard.c — system clipboard (pbcopy) and link opening (open), macOS. */
#include "clipboard.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>

int clip_copy(const char *text, size_t len) {
    FILE *p = popen("pbcopy", "w");
    if (!p) return -1;
    fwrite(text, 1, len, p);
    return pclose(p) == 0 ? 0 : -1;
}

int open_url(const char *url) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {                       /* child: exec `open <url>` */
        execlp("open", "open", url, (char *)NULL);
        _exit(127);                       /* exec failed */
    }
    int status;
    waitpid(pid, &status, 0);             /* `open` returns promptly */
    return 0;
}

/* Run argv to completion with stdout/stderr sent to /dev/null. */
static void run_quiet(char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0) return;
    if (pid == 0) {
        int dn = open("/dev/null", O_WRONLY);
        if (dn >= 0) { dup2(dn, 1); dup2(dn, 2); close(dn); }
        execvp(argv[0], argv);
        _exit(127);
    }
    int status;
    waitpid(pid, &status, 0);
}

/* Size of a file in bytes, or -1 if it doesn't exist. */
static long file_size(const char *path) {
    struct stat s;
    return stat(path, &s) == 0 ? (long)s.st_size : -1;
}

/* AppleScript writes the clipboard's image to `path` (cast to `imgclass`, e.g.
 * «class PNGf»). On a non-image clipboard the cast fails after the file is
 * opened, leaving a 0-byte file — so a positive size means real image data.
 *
 * The path is passed as an `on run argv` parameter, never interpolated into the
 * script text: a document directory named with a `"` (or newline) would
 * otherwise close the string literal and inject arbitrary AppleScript. `path`
 * being argv data, it is inert. `write_line` is always a constant from below. */
static void osascript_write(const char *path, const char *write_line) {
    char *argv[] = { "osascript",
        "-e", "on run argv",
        "-e", "set f to open for access (POSIX file (item 1 of argv)) with write permission",
        "-e", "try",
        "-e", "set eof f to 0",
        "-e", (char *)write_line,
        "-e", "close access f",
        "-e", "on error",
        "-e", "try", "-e", "close access f", "-e", "end try",
        "-e", "end try",
        "-e", "end run",
        (char *)path, NULL };
    run_quiet(argv);
}

/* Does the clipboard advertise image data? `clipboard info` lists the types it
 * holds (e.g. «class PNGf», «class TIFF») even when the bytes are still
 * "promised" and not yet produced. Checking first lets clip_image_save retry
 * only when an image really is coming, instead of stalling on a text clipboard. */
static int clip_has_image(void) {
    FILE *p = popen("osascript -e 'clipboard info' 2>/dev/null", "r");
    if (!p) return 0;
    char buf[8192];
    size_t n = fread(buf, 1, sizeof buf - 1, p);
    buf[n] = '\0';
    pclose(p);
    return strstr(buf, "PNGf") || strstr(buf, "TIFF") ||
           strstr(buf, "GIFf") || strstr(buf, "class PNG") != NULL;
}

int clip_image_save(const char *path) {
    if (!clip_has_image()) return 0;   /* no image on the clipboard — fail fast */
    char tiff[4200];
    snprintf(tiff, sizeof tiff, "%s.tiff.tmp", path);
    /* A lazily-promised pasteboard (an app produces the image only when first
     * asked) can return nothing on the first read but have it ready a moment
     * later. We already know an image is coming, so retry until it materialises
     * (up to ~1.5s) — this makes a single Ctrl-V suffice for screenshots and
     * "copy image" sources, not just files already sitting on the clipboard. */
    for (int attempt = 0; attempt < 10; attempt++) {
        /* Most image sources put PNG on the pasteboard; try that first. The «»
         * (AppleScript class quotes) are split out of the hex escapes so a
         * following hex digit isn't swallowed into the escape. */
        osascript_write(path, "write (the clipboard as \xc2\xab" "class PNGf\xc2\xbb) to f");
        if (file_size(path) > 0) return 1;
        unlink(path);

        /* Otherwise grab a TIFF (common for "copy image" in apps) and convert. */
        osascript_write(tiff, "write (the clipboard as \xc2\xab" "class TIFF\xc2\xbb) to f");
        if (file_size(tiff) > 0) {
            char *argv[] = { "sips", "-s", "format", "png", tiff, "--out", (char *)path, NULL };
            run_quiet(argv);
            unlink(tiff);
            if (file_size(path) > 0) return 1;
            unlink(path);
        } else {
            unlink(tiff);
        }
        usleep(150000);   /* 150ms between tries (~1.5s total) for a promised image */
    }
    return 0;
}
