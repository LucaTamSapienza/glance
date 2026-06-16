/* clipboard.c — system clipboard (pbcopy) and link opening (open), macOS. */
#include "clipboard.h"

#include <stdio.h>
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
 * opened, leaving a 0-byte file — so a positive size means real image data. */
static void osascript_write(const char *path, const char *write_line) {
    char openf[5000], writef[256];
    snprintf(openf, sizeof openf,
             "set f to open for access (POSIX file \"%s\") with write permission", path);
    snprintf(writef, sizeof writef, "%s", write_line);
    char *argv[] = { "osascript",
        "-e", "try",
        "-e", openf,
        "-e", "set eof f to 0",
        "-e", writef,
        "-e", "close access f",
        "-e", "on error",
        "-e", "try", "-e", "close access f", "-e", "end try",
        "-e", "end try", NULL };
    run_quiet(argv);
}

int clip_image_save(const char *path) {
    char tiff[4200];
    snprintf(tiff, sizeof tiff, "%s.tiff.tmp", path);
    /* Two rounds: a lazily-promised pasteboard (an app produces the image only
     * when first asked) can return nothing on the first read but have it ready a
     * moment later — the retry makes a single Ctrl-V suffice. */
    for (int attempt = 0; attempt < 2; attempt++) {
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
        usleep(200000);   /* 200ms: give a promised pasteboard time to materialise */
    }
    return 0;
}
