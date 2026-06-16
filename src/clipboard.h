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

#endif /* GLANCE_CLIPBOARD_H */
