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

/* What to do when the watched file changed on disk. Pure decision, split out so
 * it can be unit-tested without a terminal:
 *   RELOAD_NONE     — content is identical (e.g. our own save); do nothing
 *   RELOAD_APPLY    — adopt the disk version (no unsaved edits to lose)
 *   RELOAD_CONFLICT — disk and buffer both changed; ask the user which to keep */
typedef enum { RELOAD_NONE, RELOAD_APPLY, RELOAD_CONFLICT } ReloadAction;
ReloadAction watch_reload_action(int content_differs, int dirty);

#endif /* GLANCE_FSWATCH_H */
