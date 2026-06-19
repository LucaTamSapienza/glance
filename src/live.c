/* live.c — the WYSIWYG (Live) projection. See live.h.
 *
 * The renderer tags each visual line with the source line it came from
 * (Line.source_line, 1-based, or 0 when unknown). Those tags are monotonic
 * non-decreasing down the Doc, so the visual lines belonging to one source line
 * form a contiguous block. live_build carries the last seen tag forward to give
 * every visual line an effective source line, drops the block that matches the
 * active line, and remembers where that block sat so the raw line can be drawn
 * back in its place.
 */
#include "live.h"

#include <stdlib.h>

/* See live.h. */
int live_build(const Doc *doc, int active, LiveView *out) {
    out->lines = NULL;
    out->nline = 0;
    out->active_at = 0;
    if (doc->nline == 0) return 0;

    const Line **arr = malloc(doc->nline * sizeof *arr);
    if (!arr) return -1;

    int n = 0;          /* kept lines so far */
    int active_at = 0;  /* kept lines whose source sits before `active` */
    int cur = -1;       /* effective source line (0-based) of the line scanned */
    for (size_t i = 0; i < doc->nline; i++) {
        const Line *L = &doc->lines[i];
        if (L->source_line > 0) cur = L->source_line - 1;
        if (cur == active) continue;          /* the active line's own render: drop */
        arr[n++] = L;
        if (cur < active) active_at = n;       /* raw line goes after everything earlier */
    }

    out->lines = arr;
    out->nline = n;
    out->active_at = active_at;
    return 0;
}

/* See live.h. */
void live_free(LiveView *out) {
    free(out->lines);
    out->lines = NULL;
    out->nline = 0;
    out->active_at = 0;
}
