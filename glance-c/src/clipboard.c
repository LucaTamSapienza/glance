/* clipboard.c — system clipboard via pbcopy (macOS). */
#include "clipboard.h"

#include <stdio.h>

int clip_copy(const char *text, size_t len) {
    FILE *p = popen("pbcopy", "w");
    if (!p) return -1;
    fwrite(text, 1, len, p);
    return pclose(p) == 0 ? 0 : -1;
}
