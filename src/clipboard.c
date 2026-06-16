/* clipboard.c — system clipboard (pbcopy) and link opening (open), macOS. */
#include "clipboard.h"

#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>

int clip_copy(const char *text, size_t len) {
    FILE *p = popen("pbcopy", "w");
    if (!p) return -1;
    fwrite(text, 1, len, p);
    return pclose(p) == 0 ? 0 : -1;
}

int open_url(const char *url) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {                       /* child: exec `open <url>` */
        execlp("open", "open", url, (char *)NULL);
        _exit(127);                       /* exec failed */
    }
    int status;
    waitpid(pid, &status, 0);             /* `open` returns promptly */
    return 0;
}
