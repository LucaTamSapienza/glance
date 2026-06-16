/* image_size.c — pull pixel dimensions from an image file's header bytes.
 *
 * Each format stores its width/height a few bytes into the file, so a small
 * fixed read and a per-format check is enough — no image library, no full
 * decode. This lets the renderer (which links no graphics code) size an image
 * block by aspect ratio. */
#include "image_size.h"

#include <stdio.h>
#include <string.h>

static unsigned be16(const unsigned char *p) { return ((unsigned)p[0] << 8) | p[1]; }
static unsigned le16(const unsigned char *p) { return ((unsigned)p[1] << 8) | p[0]; }
static unsigned be32(const unsigned char *p) {
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3];
}
static unsigned le32(const unsigned char *p) {
    return ((unsigned)p[3] << 24) | ((unsigned)p[2] << 16) | ((unsigned)p[1] << 8) | p[0];
}

/* Find the dimensions in a JPEG by walking its marker segments to the SOF. */
static int jpeg_size(const unsigned char *b, size_t n, int *w, int *h) {
    size_t i = 2;
    while (i + 9 < n) {
        if (b[i] != 0xFF) { i++; continue; }
        unsigned char m = b[i + 1];
        if (m == 0xFF) { i++; continue; }                 /* fill bytes */
        if (m == 0xD8 || m == 0xD9 || (m >= 0xD0 && m <= 0xD7) || m == 0x01) { i += 2; continue; }
        if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {  /* a SOF marker */
            *h = (int)be16(b + i + 5); *w = (int)be16(b + i + 7);
            return *w > 0 && *h > 0;
        }
        unsigned seg = be16(b + i + 2);                   /* skip this segment */
        if (seg < 2) break;
        i += 2 + seg;
    }
    return 0;
}

/* Read pixel dimensions from an image header; see image_size.h. */
int img_pixel_size(const char *path, int *w, int *h) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    unsigned char b[65536];
    size_t n = fread(b, 1, sizeof b, f);
    fclose(f);
    if (n < 26) return 0;

    if (memcmp(b, "\x89PNG\r\n\x1a\n", 8) == 0 && memcmp(b + 12, "IHDR", 4) == 0) {
        *w = (int)be32(b + 16); *h = (int)be32(b + 20);
        return *w > 0 && *h > 0;
    }
    if (memcmp(b, "GIF87a", 6) == 0 || memcmp(b, "GIF89a", 6) == 0) {
        *w = (int)le16(b + 6); *h = (int)le16(b + 8);
        return *w > 0 && *h > 0;
    }
    if (b[0] == 'B' && b[1] == 'M') {
        int hh = (int)le32(b + 22);
        *w = (int)le32(b + 18); *h = hh < 0 ? -hh : hh;   /* height may be top-down (negative) */
        return *w > 0 && *h > 0;
    }
    if (b[0] == 0xFF && b[1] == 0xD8)
        return jpeg_size(b, n, w, h);
    if (n >= 30 && memcmp(b, "RIFF", 4) == 0 && memcmp(b + 8, "WEBP", 4) == 0) {
        if (memcmp(b + 12, "VP8X", 4) == 0) {
            *w = 1 + (b[24] | (b[25] << 8) | (b[26] << 16));
            *h = 1 + (b[27] | (b[28] << 8) | (b[29] << 16));
            return 1;
        }
        if (memcmp(b + 12, "VP8 ", 4) == 0) {
            *w = (int)(le16(b + 26) & 0x3fff); *h = (int)(le16(b + 28) & 0x3fff);
            return *w > 0 && *h > 0;
        }
        if (memcmp(b + 12, "VP8L", 4) == 0) {
            unsigned bits = b[21] | (b[22] << 8) | (b[23] << 16) | ((unsigned)b[24] << 24);
            *w = 1 + (int)(bits & 0x3fff); *h = 1 + (int)((bits >> 14) & 0x3fff);
            return 1;
        }
    }
    return 0;
}
