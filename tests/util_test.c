/* util_test.c — unit tests for the shared helpers in util.c. */
#include "../src/util.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Write `len` bytes of `data` to a fresh temp file; returns its path. */
static const char *tmpfile_with(const char *data, size_t len) {
    static char path[256];
    snprintf(path, sizeof path, "/tmp/glance-util-test-XXXXXX");
    int fd = mkstemp(path);
    assert(fd >= 0);
    assert(write(fd, data, len) == (ssize_t)len);
    close(fd);
    return path;
}

int main(void) {
    char hex[65];

    /* sha256: NIST vector — the empty message */
    const char *p = tmpfile_with("", 0);
    assert(sha256_file_hex(p, hex) == 0);
    assert(strcmp(hex,
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0);
    unlink(p);

    /* sha256: NIST vector — "abc" */
    p = tmpfile_with("abc", 3);
    assert(sha256_file_hex(p, hex) == 0);
    assert(strcmp(hex,
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
    unlink(p);

    /* sha256: a buffer larger than the internal 64 KB read chunk */
    size_t big = 200000;
    char *buf = malloc(big);
    assert(buf);
    memset(buf, 'x', big);
    p = tmpfile_with(buf, big);
    assert(sha256_file_hex(p, hex) == 0);
    assert(strlen(hex) == 64);            /* well-formed lowercase hex */
    for (const char *c = hex; *c; c++)
        assert((*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'f'));
    unlink(p);
    free(buf);

    /* sha256: unreadable path fails cleanly */
    assert(sha256_file_hex("/nonexistent/glance-test", hex) == -1);

    /* u8_runelen: ascii, 2/3/4-byte leads, invalid lead */
    assert(u8_runelen('a') == 1);
    assert(u8_runelen(0xC3) == 2);
    assert(u8_runelen(0xE2) == 3);
    assert(u8_runelen(0xF0) == 4);
    assert(u8_runelen(0xBF) == 1);

    /* path_resolve: absolute, relative join, URL rejection */
    char *r = path_resolve("/base", "/abs/x.md");
    assert(r && strcmp(r, "/abs/x.md") == 0);
    free(r);
    r = path_resolve("/base", "rel.md");
    assert(r && strcmp(r, "/base/rel.md") == 0);
    free(r);
    assert(path_resolve("/base", "https://x/y.png") == NULL);

    printf("all util tests passed\n");
    return 0;
}
