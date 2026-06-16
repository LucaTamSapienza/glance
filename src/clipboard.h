#ifndef GLANCE_CLIPBOARD_H
#define GLANCE_CLIPBOARD_H

#include <stddef.h>

/* Copy `len` bytes of text to the macOS system clipboard via pbcopy. Returns 0
 * on success, -1 if pbcopy couldn't be run. */
int clip_copy(const char *text, size_t len);

/* Open a URL (or file) with the system handler via `open`. Returns 0 on
 * success. The argument is passed as a separate argv entry, so it is not
 * interpreted by a shell. */
int open_url(const char *url);

/* If the system clipboard holds an image, save it as a PNG at `path` and return
 * 1; return 0 when the clipboard holds no image. Uses macOS built-ins
 * (osascript, plus sips to convert a TIFF-only clipboard). */
int clip_image_save(const char *path);

#endif /* GLANCE_CLIPBOARD_H */
