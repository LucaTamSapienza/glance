/* fs_save_test.c — unit tests for atomic_write. */
#include "../src/fs_save.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static int fails = 0;
static void expect(int cond, const char *msg) {
    if (!cond) { printf("FAIL: %s\n", msg); fails++; }
}

/* Read a whole file into buf (up to cap); returns byte count or -1. */
static long slurp(const char *path, char *buf, size_t cap) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(buf, 1, cap - 1, f);
    fclose(f);
    buf[n] = '\0';
    return (long)n;
}

int main(void) {
    char path[] = "/tmp/glance_fs_save_test.md";
    unlink(path);

    expect(atomic_write(path, "hello\nworld\n", 12) == 0, "write new file");
    char buf[256];
    expect(slurp(path, buf, sizeof buf) == 12 && strcmp(buf, "hello\nworld\n") == 0,
           "content round-trips");

    /* overwrite preserves the file's mode (set it unusual, then rewrite) */
    chmod(path, 0640);
    expect(atomic_write(path, "second", 6) == 0, "overwrite existing");
    struct stat st;
    expect(stat(path, &st) == 0 && (st.st_mode & 07777) == 0640, "mode preserved");
    expect(slurp(path, buf, sizeof buf) == 6 && strcmp(buf, "second") == 0, "new content");

    expect(atomic_write("/no/such/dir/file.md", "x", 1) == -1, "bad path fails");

    unlink(path);
    if (fails) { printf("%d fs_save test(s) FAILED\n", fails); return 1; }
    printf("all fs_save tests passed\n");
    return 0;
}
