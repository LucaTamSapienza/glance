/* tui.c — slice 2: Reader-mode TUI on notcurses.
 *
 * Renders the Markdown Doc into a scrollable view and handles navigation keys.
 * Runs blit Doc runs straight to notcurses cells (no ANSI round-trip). On
 * resize the document is re-rendered at the new width. This is the C analogue
 * of bubbletea's viewport + glamour preview, with the renderer being ours.
 */
#include "tui.h"
#include "render.h"

#include <notcurses/notcurses.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    struct notcurses *nc;
    struct ncplane   *plane;     /* standard plane */
    const char *src; size_t srclen;
    const char *title;
    Doc  *doc;
    int   top;                   /* index of first visible doc line */
    int   rows, cols;            /* terminal size */
    int   dark;
} App;

static int content_rows(App *a) { return a->rows - 1; }   /* last row = status */

/* clamp scroll so we never run past the end / before the start */
static void clamp_top(App *a) {
    int max = (int)a->doc->nline - content_rows(a);
    if (max < 0) max = 0;
    if (a->top > max) a->top = max;
    if (a->top < 0) a->top = 0;
}

static void apply_style(struct ncplane *p, const Style *st) {
    unsigned s = 0;
    if (st->bold)      s |= NCSTYLE_BOLD;
    if (st->italic)    s |= NCSTYLE_ITALIC;
    if (st->underline) s |= NCSTYLE_UNDERLINE;
    if (st->strike)    s |= NCSTYLE_STRUCK;
    ncplane_set_styles(p, s);
    if (st->has_fg) ncplane_set_fg_rgb8(p, st->fg.r, st->fg.g, st->fg.b);
    else            ncplane_set_fg_default(p);
    if (st->has_bg) ncplane_set_bg_rgb8(p, st->bg.r, st->bg.g, st->bg.b);
    else            ncplane_set_bg_default(p);
}

static void draw(App *a) {
    struct ncplane *p = a->plane;
    ncplane_erase(p);

    int body = content_rows(a);
    for (int row = 0; row < body; row++) {
        int li = a->top + row;
        if (li >= (int)a->doc->nline) break;
        Line *L = &a->doc->lines[li];

        /* code/quote fill: lay a background band first */
        if (L->fill) {
            ncplane_set_fg_default(p);
            ncplane_set_bg_rgb8(p, L->fill_bg.r, L->fill_bg.g, L->fill_bg.b);
            ncplane_set_styles(p, NCSTYLE_NONE);
            for (int x = 0; x < a->cols; x++)
                ncplane_putchar_yx(p, row, x, ' ');
        }

        int x = 0;
        for (size_t j = 0; j < L->nrun && x < a->cols; j++) {
            apply_style(p, &L->runs[j].st);
            ncplane_putstr_yx(p, row, x, L->runs[j].text);
            /* advance by the run's display width (cols), computed from text */
            int w = 0;
            for (size_t k = 0; k < L->runs[j].len; k++)
                if (((unsigned char)L->runs[j].text[k] & 0xC0) != 0x80) w++;
            x += w;
        }
    }

    /* status bar */
    ncplane_set_styles(p, NCSTYLE_BOLD);
    ncplane_set_fg_rgb8(p, 20, 20, 20);
    ncplane_set_bg_rgb8(p, a->dark ? 150 : 60, a->dark ? 150 : 60, a->dark ? 150 : 60);
    for (int x = 0; x < a->cols; x++) ncplane_putchar_yx(p, a->rows - 1, x, ' ');
    int total = (int)a->doc->nline;
    int shown_end = a->top + body; if (shown_end > total) shown_end = total;
    char status[256];
    int pct = total > 0 ? (shown_end * 100) / total : 100;
    snprintf(status, sizeof status, " glance  %s  —  %d–%d/%d  (%d%%)   q quit  j/k scroll  g/G top/bottom",
             a->title ? a->title : "", total ? a->top + 1 : 0, shown_end, total, pct);
    ncplane_putstr_yx(p, a->rows - 1, 0, status);

    ncplane_set_styles(p, NCSTYLE_NONE);
    ncplane_set_fg_default(p);
    ncplane_set_bg_default(p);
    notcurses_render(a->nc);
}

/* (re)render the document at the current terminal width */
static int rerender(App *a) {
    unsigned r, c;
    ncplane_dim_yx(a->plane, &r, &c);
    a->rows = (int)r; a->cols = (int)c;
    if (a->doc) doc_free(a->doc);
    a->doc = render_doc(a->src, a->srclen, a->cols, a->dark);
    if (!a->doc) return -1;
    clamp_top(a);
    return 0;
}

int tui_run(const char *src, unsigned long len, const char *title) {
    notcurses_options opts;
    memset(&opts, 0, sizeof opts);
    opts.flags = NCOPTION_SUPPRESS_BANNERS;

    App a;
    memset(&a, 0, sizeof a);
    a.src = src; a.srclen = len; a.title = title; a.dark = 1;

    a.nc = notcurses_init(&opts, NULL);
    if (!a.nc) return 1;
    a.plane = notcurses_stdplane(a.nc);

    if (rerender(&a) != 0) { notcurses_stop(a.nc); return 1; }
    draw(&a);

    ncinput ni;
    uint32_t id;
    while ((id = notcurses_get_blocking(a.nc, &ni)) != (uint32_t)-1) {
        if (ni.evtype == NCTYPE_RELEASE) continue;   /* key-down only */
        int body = content_rows(&a);
        int handled = 1;

        if (id == NCKEY_RESIZE) {
            rerender(&a);
        } else if (id == 'q' || id == NCKEY_ESC) {
            break;
        } else if (id == 'j' || id == NCKEY_DOWN) {
            a.top++;
        } else if (id == 'k' || id == NCKEY_UP) {
            a.top--;
        } else if (id == NCKEY_PGDOWN || (id == 'f' && ncinput_ctrl_p(&ni))) {
            a.top += body;
        } else if (id == NCKEY_PGUP || (id == 'b' && ncinput_ctrl_p(&ni))) {
            a.top -= body;
        } else if (id == 'd' && ncinput_ctrl_p(&ni)) {
            a.top += body / 2;
        } else if (id == 'u' && ncinput_ctrl_p(&ni)) {
            a.top -= body / 2;
        } else if (id == 'g' || id == NCKEY_HOME) {
            a.top = 0;
        } else if (id == 'G' || id == NCKEY_END) {
            a.top = (int)a.doc->nline;   /* clamp pins it to the last page */
        } else {
            handled = 0;
        }

        if (handled) { clamp_top(&a); draw(&a); }
    }

    doc_free(a.doc);
    notcurses_stop(a.nc);
    return 0;
}
