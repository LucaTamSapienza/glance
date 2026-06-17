/* progress.c — pure logic for the Reader scroll/progress HUD (see progress.h).
 * No notcurses, no md4c, no clock: just arithmetic and a glyph table. */
#include "progress.h"

int progress_percent(int line, int nline) {
    if (nline <= 1) return 100;
    if (line < 0) line = 0;
    if (line >= nline) line = nline - 1;
    int pct = (int)((long)line * 100 / (nline - 1));
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}

void progress_scroll(int *top, int *line, int dir, int nline, int body) {
    int maxtop = nline - body;
    if (maxtop < 0) maxtop = 0;
    int t = *top + dir;
    if (t < 0) t = 0;
    if (t > maxtop) t = maxtop;
    int l = *line + dir;
    if (l < 0) l = 0;
    if (l >= nline) l = nline - 1;
    if (l < 0) l = 0;            /* nline == 0 guard */
    *top = t;
    *line = l;
}

/* The dots-ring, in rotation order. */
static const char *FRAMES[6] = {
    "\xe2\x97\x9c",  /* ◜ */
    "\xe2\x97\xa0",  /* ◠ */
    "\xe2\x97\x9d",  /* ◝ */
    "\xe2\x97\x9e",  /* ◞ */
    "\xe2\x97\xa1",  /* ◡ */
    "\xe2\x97\x9f",  /* ◟ */
};

const char *progress_spinner(int frame) {
    int i = frame % 6;
    if (i < 0) i += 6;
    return FRAMES[i];
}
