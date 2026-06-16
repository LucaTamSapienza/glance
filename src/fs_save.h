#ifndef GLANCE_FS_SAVE_H
#define GLANCE_FS_SAVE_H

#include <stddef.h>

/* Write `len` bytes to `path` atomically: data goes to a temp file in the same
 * directory, is flushed, then renamed over `path` (rename is atomic on the same
 * filesystem, so a reader never sees a half-written file). The original file's
 * permission bits are preserved when it already exists. Returns 0 on success,
 * -1 on error (errno set). Ported from the Go app's fs/save.go.
 */
int atomic_write(const char *path, const char *data, size_t len);

#endif /* GLANCE_FS_SAVE_H */
