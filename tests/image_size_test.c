/* image_size_test.c — header parsing for PNG/GIF/BMP/JPEG dimension reads. */
#include "../src/image_size.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fails = 0;
static void expect(int cond, const char *msg) {
    if (!cond) { printf("FAIL: %s\n", msg); fails++; }
}

/* Write bytes to a temp file and return its path (static buffer). */
static const char *tmpwrite(const unsigned char *data, size_t n) {
    static char path[] = "/tmp/glance_img_XXXXXX";
    strcpy(path, "/tmp/glance_img_XXXXXX");
    int fd = mkstemp(path);
    if (fd < 0) return NULL;
    if (write(fd, data, n) != (ssize_t)n) { close(fd); return NULL; }
    close(fd);
    return path;
}

/* Assert img_pixel_size reads (ew, eh) from a header. */
static void check(const unsigned char *data, size_t n, int ew, int eh, const char *name) {
    const char *p = tmpwrite(data, n);
    if (!p) { printf("FAIL: tmp write %s\n", name); fails++; return; }
    int w = 0, h = 0, ok = img_pixel_size(p, &w, &h);
    unlink(p);
    expect(ok && w == ew && h == eh, name);
}

int main(void) {
    /* PNG: 8-byte signature, then IHDR with width=300 height=200 big-endian. */
    unsigned char png[33] = {
        0x89,'P','N','G',0x0d,0x0a,0x1a,0x0a, 0,0,0,13, 'I','H','D','R',
        0,0,1,44,  0,0,0,200,  8,2,0,0,0, 0,0,0,0 };
    check(png, sizeof png, 300, 200, "PNG dimensions");

    /* GIF: 'GIF89a' then width=640 height=480 little-endian. */
    unsigned char gif[26] = { 'G','I','F','8','9','a', 0x80,0x02, 0xe0,0x01 };
    check(gif, sizeof gif, 640, 480, "GIF dimensions");

    /* BMP: 'BM', width@18=128 height@22=64 little-endian. */
    unsigned char bmp[30] = {0};
    bmp[0] = 'B'; bmp[1] = 'M';
    bmp[18] = 128; bmp[22] = 64;
    check(bmp, sizeof bmp, 128, 64, "BMP dimensions");

    /* JPEG: SOI, an APP0 segment, then SOF0 with height=100 width=250. */
    unsigned char jpg[] = {
        0xFF,0xD8,                               /* SOI */
        0xFF,0xE0, 0,4, 'J','F',                 /* APP0, length 4 */
        0xFF,0xC0, 0,17, 8, 0,100, 0,250, 3,     /* SOF0: h=100 w=250 */
        1,0x22,0, 2,0x11,1, 3,0x11,1, 0,0,0 };
    check(jpg, sizeof jpg, 250, 100, "JPEG dimensions");

    /* Unrecognised data yields 0. */
    unsigned char junk[40] = { 'n','o','t','a','n','i','m','a','g','e' };
    int w, h;
    const char *p = tmpwrite(junk, sizeof junk);
    if (p) { expect(img_pixel_size(p, &w, &h) == 0, "unknown format -> 0"); unlink(p); }
    expect(img_pixel_size("/no/such/file.png", &w, &h) == 0, "missing file -> 0");

    if (fails) { printf("%d image_size test(s) FAILED\n", fails); return 1; }
    printf("all image_size tests passed\n");
    return 0;
}
