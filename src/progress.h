/* progress.h — pure logic for the Reader's scroll/reading-progress HUD.
 *
 * The notcurses drawing, the mouse wiring, and the spin-down timing live in
 * tui.c; the parts that can be reasoned about without a terminal — the percent
 * readout, the ride-along scroll step, and the spinner frame table — live here
 * so they can be unit tested. */
#ifndef PROGRESS_H
#define PROGRESS_H

#define PROGRESS_REST "\xe2\x97\x8c"   /* ◌ — neutral glyph shown at rest */

/* Reading position as a percentage of the document, clamped to 0..100. `line`
 * is the 0-based cursor line, `nline` the total; nline <= 1 yields 100. */
int progress_percent(int line, int nline);

/* The ride-along scroll step: move the viewport `top` and the cursor `line`
 * together by `dir` lines, clamping top to [0, max(0, nline - body)] and line
 * to [0, nline - 1]. Keeps the cursor at the same screen row so it stays
 * visible. */
void progress_scroll(int *top, int *line, int dir, int nline, int body);

/* One frame of the dots-ring spinner (◜ ◠ ◝ ◞ ◡ ◟), indexed mod 6 (negative
 * frames wrap too). Returns a 3-byte UTF-8 glyph, one display column wide. */
const char *progress_spinner(int frame);

#endif
