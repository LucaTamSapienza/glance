/* fswatch.c — watch a file's parent directory for changes (kqueue/macOS). */
#include "fswatch.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>
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
