#ifndef GLANCE_LIVE_H
#define GLANCE_LIVE_H

#include "render.h"

/* The WYSIWYG ("Live") projection of a document: the rendered Doc with the
 * source line under the cursor pulled out, so the user edits raw Markdown in
 * place on that one line while every other line stays styled — the single-mode,
 * editxr-style editing UX (the north-star "Notion/Obsidian in the terminal").
 *
 * live.c only partitions a Doc around the active source line; it is pure and
 * notcurses-free, so it is unit-testable on its own. tui.c blits the kept
 * rendered lines with the normal Doc blitter and draws the active line raw with
 * the editor pane, putting the hardware cursor on it.
 */

typedef struct {
    const Line **lines;  /* rendered lines to blit, in visual order. The pointers
                            borrow the Doc's Lines (do not free them); only the
                            array itself is owned. */
    int  nline;          /* number of entries in `lines` */
    int  active_at;      /* index in `lines` where the raw active line is drawn
                            (0..nline). The active source line's own rendered
                            lines are excluded from `lines`, so the visual order
                            is: lines[0..active_at-1], raw line, lines[active_at..]. */
} LiveView;

/* Build the live projection of `doc` for the 0-based source line `active`.
 * Every rendered line whose source maps to `active` is dropped from `out->lines`,
 * and `out->active_at` records where, in the kept sequence, the raw line belongs
 * so the visual order is preserved — this works even when `active` has no
 * rendered line of its own (e.g. a blank line, or one md4c folds away). Returns
 * 0 on success, -1 on OOM. Free with live_free. */
int  live_build(const Doc *doc, int active, LiveView *out);

/* Release the array allocated by live_build (the borrowed Lines are untouched). */
void live_free(LiveView *out);

#endif /* GLANCE_LIVE_H */
