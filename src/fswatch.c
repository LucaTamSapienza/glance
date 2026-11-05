/* fswatch.c — watch a file's parent directory for changes (kqueue on macOS,
 * inotify on Linux). */
#include "fswatch.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>

#ifdef __linux__
#include <sys/inotify.h>

/* Begin watching the directory containing `path` with inotify; `kq` holds the
 * inotify fd to poll and `dir` the watch descriptor. */
int watch_open(Watch *w, const char *path) {
    w->kq = w->dir = -1;
    if (!path) return -1;

    char buf[4096];
    snprintf(buf, sizeof buf, "%s", path);
    int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (fd < 0) return -1;

    int wd = inotify_add_watch(fd, dirname(buf),   /* dirname may rewrite buf */
                               IN_MODIFY | IN_ATTRIB | IN_CLOSE_WRITE | IN_CREATE |
                               IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO);
    if (wd < 0) { close(fd); return -1; }

    w->kq = fd; w->dir = wd;
    return fd;
}

/* Consume pending inotify events after the fd polls readable. */
void watch_drain(Watch *w) {
    if (w->kq < 0) return;
    char buf[4096];
    while (read(w->kq, buf, sizeof buf) > 0) { /* discard */ }
}

/* Stop watching; closing the inotify fd drops its watch descriptor too. */
void watch_close(Watch *w) {
    if (w->kq >= 0) close(w->kq);
    w->kq = w->dir = -1;
}

#else
#include <sys/event.h>

int watch_open(Watch *w, const char *path) {
    w->kq = w->dir = -1;
    if (!path) return -1;

    char buf[4096];
    snprintf(buf, sizeof buf, "%s", path);
    int dfd = open(dirname(buf), O_RDONLY);   /* dirname may rewrite buf */
    if (dfd < 0) return -1;

    int kq = kqueue();
    if (kq < 0) { close(dfd); return -1; }

    struct kevent ev;
    EV_SET(&ev, dfd, EVFILT_VNODE, EV_ADD | EV_CLEAR,
           NOTE_WRITE | NOTE_DELETE | NOTE_RENAME | NOTE_EXTEND | NOTE_ATTRIB, 0, NULL);
    if (kevent(kq, &ev, 1, NULL, 0, NULL) < 0) { close(dfd); close(kq); return -1; }

    w->kq = kq; w->dir = dfd;
    return kq;
}

void watch_drain(Watch *w) {
    if (w->kq < 0) return;
    struct kevent ev;
    struct timespec zero = {0, 0};
    while (kevent(w->kq, NULL, 0, &ev, 1, &zero) > 0) { /* discard */ }
}

void watch_close(Watch *w) {
    if (w->dir >= 0) close(w->dir);
    if (w->kq >= 0) close(w->kq);
    w->kq = w->dir = -1;
}
#endif

ReloadAction watch_reload_action(int content_differs, int dirty) {
    if (!content_differs) return RELOAD_NONE;   /* identical (often our own save) */
    if (!dirty) return RELOAD_APPLY;            /* nothing local to lose: live-sync */
    return RELOAD_CONFLICT;                      /* both sides changed: ask the user */
}
