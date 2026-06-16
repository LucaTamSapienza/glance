/* fs_save.c — atomic file writes (temp + rename), see fs_save.h. */
#include "fs_save.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

/* Write the whole buffer to fd, retrying short writes. Returns 0 on success. */
static int write_all(int fd, const char *data, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(fd, data + off, len - off);
        if (w < 0) return -1;
        off += (size_t)w;
    }
    return 0;
}

int atomic_write(const char *path, const char *data, size_t len) {
    /* temp file alongside the target so rename() stays on one filesystem */
    char tmp[4096];
    if (snprintf(tmp, sizeof tmp, "%s.glance-XXXXXX", path) >= (int)sizeof tmp)
        return -1;
    int fd = mkstemp(tmp);
    if (fd < 0) return -1;

    if (write_all(fd, data, len) != 0) { close(fd); unlink(tmp); return -1; }
    fsync(fd);
    close(fd);

    /* match the original file's permissions, or fall back to 0644 */
    struct stat st;
    chmod(tmp, stat(path, &st) == 0 ? (st.st_mode & 07777) : 0644);

    if (rename(tmp, path) != 0) { unlink(tmp); return -1; }
    return 0;
}
