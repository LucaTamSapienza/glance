/* util.c — shared UTF-8 and file helpers (see util.h). */
#include "util.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* True if b is a UTF-8 continuation byte. */
int u8_cont(unsigned char b) { return (b & 0xC0) == 0x80; }

/* Bytes in the UTF-8 sequence led by b; invalid lead bytes count as 1. */
int u8_runelen(unsigned char b) {
    if (b < 0x80)        return 1;
    if ((b & 0xE0) == 0xC0) return 2;
    if ((b & 0xF0) == 0xE0) return 3;
    if ((b & 0xF8) == 0xF0) return 4;
    return 1;
}

/* Count display columns: one per codepoint, i.e. every non-continuation byte. */
int u8_width(const char *s, size_t n) {
    int w = 0;
    for (size_t i = 0; i < n; i++)
        if (!u8_cont((unsigned char)s[i])) w++;
    return w;
}

/* Encode one codepoint as UTF-8; returns the byte count (1..4). */
int u8_encode(uint32_t cp, char *out) {
    if (cp < 0x80)    { out[0] = (char)cp; return 1; }
    if (cp < 0x800)   { out[0] = 0xC0 | (cp >> 6);  out[1] = 0x80 | (cp & 0x3F); return 2; }
    if (cp < 0x10000) { out[0] = 0xE0 | (cp >> 12); out[1] = 0x80 | ((cp >> 6) & 0x3F);
                        out[2] = 0x80 | (cp & 0x3F); return 3; }
    out[0] = 0xF0 | (cp >> 18); out[1] = 0x80 | ((cp >> 12) & 0x3F);
    out[2] = 0x80 | ((cp >> 6) & 0x3F); out[3] = 0x80 | (cp & 0x3F); return 4;
}

/* Slurp a whole stream, doubling the buffer as needed. */
char *read_file(FILE *f, size_t *len) {
    size_t cap = 1 << 16, n = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;
    for (size_t r; (r = fread(buf + n, 1, cap - n, f)) > 0; ) {
        n += r;
        if (n == cap) {
            char *p = realloc(buf, cap *= 2);
            if (!p) { free(buf); return NULL; }
            buf = p;
        }
    }
    buf[n] = '\0';   /* n < cap here: the buffer grows whenever it fills */
    *len = n;
    return buf;
}

/* Resolve a path against a base directory; see util.h. */
char *path_resolve(const char *basedir, const char *src) {
    if (!src) return NULL;
    if (strncmp(src, "http://", 7) == 0 || strncmp(src, "https://", 8) == 0) return NULL;
    if (src[0] == '/' || !basedir || !*basedir) return strdup(src);
    size_t need = strlen(basedir) + 1 + strlen(src) + 1;
    char *out = malloc(need);
    if (out) snprintf(out, need, "%s/%s", basedir, src);
    return out;
}
