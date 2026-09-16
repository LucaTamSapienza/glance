/* preview_test.c — UI invocation and relocatable companion discovery. */
#include "../src/preview.h"
#include "../src/theme.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Create an empty fixture file with a chosen mode. */
static void file(const char *path, int mode) {
    FILE *f = fopen(path, "w");
    assert(f);
    fclose(f);
    assert(chmod(path, mode) == 0);
}

/* Check both UI spellings, malformed requests, and untouched agent commands. */
static void test_args(void) {
    const char *path;
    char *first[] = {"glance", "--ui", "notes with spaces.md"};
    assert(preview_args(3, first, &path) == 1);
    assert(!strcmp(path, first[2]));
    char *last[] = {"glance", "notes.md", "--ui"};
    assert(preview_args(3, last, &path) == 1);
    assert(!strcmp(path, last[1]));
    char *missing[] = {"glance", "--ui"};
    assert(preview_args(2, missing, &path) == -1 && !path);
    char *extra[] = {"glance", "--ui", "one.md", "two.md"};
    assert(preview_args(4, extra, &path) == -1);
    char *flag[] = {"glance", "--ui", "--bad"};
    assert(preview_args(3, flag, &path) == -1);
    char *agent[] = {"glance", "--context", "--ui"};
    assert(preview_args(3, agent, &path) == 0);
    char *tui[] = {"glance", "notes.md"};
    assert(preview_args(2, tui, &path) == 0);
    assert(preview_args(1, tui, &path) == 0);
}

/* Discover development, release and installed apps, including symlinked CLIs. */
static void test_discovery(void) {
    char tmp[] = "/tmp/glance-preview-test-XXXXXX";
    assert(mkdtemp(tmp));
    char *cwd = getcwd(NULL, 0);
    assert(cwd && chdir(tmp) == 0);
    file("glance", 0755);
    assert(!preview_app_path("glance"));
    assert(mkdir("build", 0700) == 0);
    assert(mkdir("build/Glance.app", 0700) == 0);
    assert(mkdir("build/Glance.app/Contents", 0700) == 0);
    assert(mkdir("build/Glance.app/Contents/MacOS", 0700) == 0);
    file("build/Glance.app/Contents/MacOS/Glance", 0644);
    assert(!preview_app_path("glance"));
    assert(chmod("build/Glance.app/Contents/MacOS/Glance", 0755) == 0);
    char *app = preview_app_path("glance");
    assert(app && strstr(app, "/build/Glance.app"));
    free(app);
    assert(rename("build/Glance.app", "Glance.app") == 0);
    app = preview_app_path("glance");
    assert(app && strstr(app, "/Glance.app") && !strstr(app, "/build/"));
    free(app);
    assert(mkdir("bin", 0700) == 0);
    assert(mkdir("libexec", 0700) == 0);
    assert(mkdir("libexec/glance", 0700) == 0);
    assert(rename("glance", "bin/glance") == 0);
    assert(rename("Glance.app", "libexec/glance/Glance.app") == 0);
    assert(symlink("bin/glance", "linked glance") == 0);
    app = preview_app_path("linked glance");
    assert(app && strstr(app, "/libexec/glance/Glance.app"));
    free(app);
    assert(!preview_app_path("missing"));
    unlink("libexec/glance/Glance.app/Contents/MacOS/Glance");
    rmdir("libexec/glance/Glance.app/Contents/MacOS");
    rmdir("libexec/glance/Glance.app/Contents");
    rmdir("libexec/glance/Glance.app");
    rmdir("libexec/glance");
    rmdir("libexec");
    unlink("linked glance");
    unlink("bin/glance");
    rmdir("bin");
    rmdir("build");
    assert(chdir(cwd) == 0);
    free(cwd);
    assert(rmdir(tmp) == 0);
}

/* Run launcher contract tests without opening a graphical application. */
int main(void) {
    test_args();
    test_discovery();
    theme_load_config("theme = nord\n");
    int dark = -1;
    for (int system_dark = 0; system_dark <= 1; system_dark++) {
        char *page = preview_html("", 0, "Default", NULL, system_dark, &dark);
        assert(page && dark == system_dark);
        assert(strstr(page, system_dark ? "background:#1b1b1f" : "background:#ffffff"));
        free(page);
        page = preview_html("", 0, "Auto", "auto", system_dark, &dark);
        assert(page && dark == system_dark);
        free(page);
    }
    char *html = preview_html("# Preview\n", 10, "A & B", "github-light", 1, &dark);
    assert(html && !dark && strstr(html, "<h1>Preview</h1>"));
    assert(strstr(html, "<title>A &amp; B</title>"));
    free(html);
    html = preview_html("", 0, "Empty", "nord", 0, &dark);
    assert(html && dark && strstr(html, "</html>"));
    free(html);
    puts("all preview tests passed");
    return 0;
}
