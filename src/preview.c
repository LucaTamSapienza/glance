/* preview.c — launch the native macOS companion without entering the TUI. */
#include "preview.h"
#include "doc_html.h"
#include "theme.h"
#include "util.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <spawn.h>
#include <sys/wait.h>
extern char **environ;
#endif

/* Keep renderer types on the C side: AppKit also defines a type named Style. */
char *preview_html(const char *src, size_t len, const char *title,
                   const char *theme_name, int system_dark, int *actual_dark) {
    const char *home = getenv("HOME");
    if (home) {
        char path[PATH_MAX];
        int n = snprintf(path, sizeof path, "%s/.config/glance/config", home);
        if (n > 0 && (size_t)n < sizeof path) {
            FILE *f = fopen(path, "rb");
            if (f) {
                size_t size;
                char *config = read_file(f, &size);
                fclose(f);
                if (config) { theme_load_config(config); free(config); }
            }
        }
    }
    const char *name = theme_name ? theme_name : "auto";
    const Theme *theme = theme_by_name(name);
    if (!theme) theme = theme_auto(system_dark);
    if (actual_dark) *actual_dark = theme->dark;
    return md_to_html(src, len, theme, title);
}

/* Recognize only the two documented positions, leaving other commands alone. */
int preview_args(int argc, char **argv, const char **path) {
    *path = NULL;
    int first = argc > 1 && !strcmp(argv[1], "--ui");
    int second = argc > 2 && argv[1][0] != '-' && !strcmp(argv[2], "--ui");
    if (!first && !second) return 0;
    if (argc != 3) return -1;
    const char *file = argv[first ? 2 : 1];
    if (!*file || file[0] == '-') return -1;
    *path = file;
    return 1;
}

/* Resolve a built, unpacked, or installed bundle relative to the real binary. */
char *preview_app_path(const char *executable) {
    char *resolved = realpath(executable, NULL);
    if (!resolved) return NULL;
    char *slash = strrchr(resolved, '/');
    if (!slash) { free(resolved); return NULL; }
    *slash = '\0';
    const char *locations[] = {
        "build/Glance.app", "Glance.app", "../libexec/glance/Glance.app"
    };
    char *result = NULL;
    for (size_t i = 0; i < sizeof locations / sizeof locations[0]; i++) {
        char app[PATH_MAX], binary[PATH_MAX];
        int n = snprintf(app, sizeof app, "%s/%s", resolved, locations[i]);
        if (n < 0 || (size_t)n >= sizeof app) continue;
        n = snprintf(binary, sizeof binary, "%s/Contents/MacOS/Glance", app);
        if (n < 0 || (size_t)n >= sizeof binary) continue;
        struct stat st;
        if (stat(binary, &st) == 0 && S_ISREG(st.st_mode) && access(binary, X_OK) == 0) {
            result = realpath(app, NULL);
            break;
        }
    }
    free(resolved);
    return result;
}

/* Validate the input before asking Launch Services to start an independent app. */
int preview_open(const char *path, const char *theme_name) {
#ifdef __APPLE__
    char *file = realpath(path, NULL);
    if (!file) { perror(path); return 1; }
    struct stat st;
    if (stat(file, &st) != 0 || !S_ISREG(st.st_mode) || access(file, R_OK) != 0) {
        fprintf(stderr, "glance --ui: not a readable regular file: %s\n", path);
        free(file);
        return 1;
    }
    uint32_t size = 0;
    _NSGetExecutablePath(NULL, &size);
    char *executable = malloc(size);
    char *app = NULL;
    if (executable && _NSGetExecutablePath(executable, &size) == 0)
        app = preview_app_path(executable);
    free(executable);
    if (!app) {
        fprintf(stderr, "glance --ui: Glance.app not found; run make (or make install) "
                        "to build and install the preview app.\n");
        free(file);
        return 1;
    }
    char *args[] = { "/usr/bin/open", "-n", "-a", app, "--args", file,
                     theme_name ? "--theme" : NULL, (char *)theme_name, NULL };
    pid_t pid;
    int err = posix_spawn(&pid, args[0], NULL, NULL, args, environ);
    free(app);
    free(file);
    if (err) { errno = err; perror("glance --ui: open"); return 1; }
    int status;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR) continue;
        perror("glance --ui: waitpid");
        return 1;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : 1;
#else
    (void)path; (void)theme_name;
    fprintf(stderr, "glance --ui is available only on macOS.\n");
    return 1;
#endif
}
