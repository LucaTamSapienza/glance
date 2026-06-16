#ifndef GLANCE_UTIL_H
#define GLANCE_UTIL_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Shared low-level helpers: UTF-8 inspection and whole-file reads. These live
 * in one place so the renderer, editor, and TUI agree on how bytes map to
 * display columns and codepoints. Width is approximated as one column per
 * codepoint; real wide/zero-width handling is a future refinement, and keeping
 * it behind u8_width() means there is a single spot to change. */

/* True if b is a UTF-8 continuation byte (0b10xxxxxx). */
int u8_cont(unsigned char b);

/* Number of bytes in the UTF-8 sequence introduced by lead byte b (1..4). */
int u8_runelen(unsigned char b);

/* Display width, in columns, of the n-byte UTF-8 span s. */
int u8_width(const char *s, size_t n);

/* Encode codepoint cp as UTF-8 into out (>= 4 bytes); returns bytes written. */
int u8_encode(uint32_t cp, char *out);

/* Read all of stream f into a malloc'd, NUL-terminated buffer; *len gets the
 * byte length. Returns NULL on allocation failure. */
char *read_file(FILE *f, size_t *len);

/* Resolve `src` against directory `basedir`: an absolute path is copied as-is,
 * a relative one is joined onto basedir (or the cwd when basedir is NULL), and a
 * URL (http/https) yields NULL. Returns a malloc'd path the caller frees, or
 * NULL for a URL / allocation failure. */
char *path_resolve(const char *basedir, const char *src);

#endif /* GLANCE_UTIL_H */
