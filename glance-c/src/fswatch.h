#ifndef GLANCE_FSWATCH_H
#define GLANCE_FSWATCH_H

/* Watch a file for external changes via kqueue. We watch the *parent directory*
 * (not the file inode) because atomic saves replace the file with a rename, so
 * an inode-level watch would go stale — mirroring the Go app's fs/watcher.go.
 * The returned fd can be poll()ed alongside the terminal input fd. */

typedef struct { int kq, dir; } Watch;

/* Begin watching the directory containing `path`. Returns the kqueue fd to poll
 * (>= 0), or -1 if it couldn't be set up (e.g. path is NULL for stdin). */
int  watch_open(Watch *w, const char *path);

/* Consume pending events after the fd polls readable (events are level-cleared). */
void watch_drain(Watch *w);

void watch_close(Watch *w);

#endif /* GLANCE_FSWATCH_H */
