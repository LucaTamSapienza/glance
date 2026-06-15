/* tui.c — notcurses front-end. Slice 2 added Reader mode; slice 3 adds Insert.
 *
 * Two modes share one standard plane:
 *   Reader  — the rendered Doc, scrollable (slice 2).
 *   Insert  — the raw Markdown in our line-array editor, with a live cursor.
 * 'e' enters Insert, Esc returns to Reader and re-renders from the edited text.
 * The app owns a mutable copy of the source; the editor syncs back into it on
 * exit from Insert. Runs blit to cells directly — no ANSI round-trip.
 */
#include "tui.h"
#include "render.h"
#include "editor.h"

#include <notcurses/notcurses.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>

enum { MODE_READER, MODE_INSERT };

typedef struct {
    struct notcurses *nc;
    struct ncplane   *plane;
    char  *src; size_t srclen;   /* owned, mutable copy of the document */
    const char *title;
    Doc   *doc;
    int    top;                  /* Reader: first visible doc line */
    int    rows, cols;
    int    dark;
    int    mode;
    Editor ed;
} App;

static int content_rows(App *a) { return a->rows - 1; }   /* last row = status */

/* ---- terminal teardown (kitty keyboard protocol, see slice-2 fix) --------- */

static struct notcurses *g_nc = NULL;

static void term_kbd_reset(void) {
    /* CSI < u : pop kitty keyboard stack;  CSI = 0 u : force all flags off;
     * CSI > 4 ; 0 m : disable xterm modifyOtherKeys. Together these undo every
     * enhanced-input mode notcurses may have enabled, so the shell receives
     * plain keys again. Non-supporting terminals ignore unknown CSI. */
    const char *seq = "\033[<u\033[=0u\033[>4;0m";
    ssize_t w = write(STDOUT_FILENO, seq, strlen(seq));
    (void)w;
}
static void shutdown_tui(void) {
    if (g_nc) { notcurses_stop(g_nc); g_nc = NULL; }
    term_kbd_reset();
}
static void on_fatal_signal(int sig) {
    shutdown_tui();
    signal(sig, SIG_DFL);
    raise(sig);
}

/* ---- shared helpers ------------------------------------------------------- */

static int run_cols(const Run *r) {
    int w = 0;
    for (size_t k = 0; k < r->len; k++)
        if (((unsigned char)r->text[k] & 0xC0) != 0x80) w++;
    return w;
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

static void status_bar(App *a, const char *text) {
    struct ncplane *p = a->plane;
    ncplane_set_styles(p, NCSTYLE_BOLD);
    ncplane_set_fg_rgb8(p, 20, 20, 20);
    int g = a->dark ? 150 : 80;
    ncplane_set_bg_rgb8(p, g, g, g);
    for (int x = 0; x < a->cols; x++) ncplane_putchar_yx(p, a->rows - 1, x, ' ');
    ncplane_putstr_yx(p, a->rows - 1, 0, text);
    ncplane_set_styles(p, NCSTYLE_NONE);
    ncplane_set_fg_default(p);
    ncplane_set_bg_default(p);
}

/* ---- Reader mode ---------------------------------------------------------- */

static void clamp_top(App *a) {
    int max = (int)a->doc->nline - content_rows(a);
    if (max < 0) max = 0;
    if (a->top > max) a->top = max;
    if (a->top < 0) a->top = 0;
}

static void draw_reader(App *a) {
    struct ncplane *p = a->plane;
    notcurses_cursor_disable(a->nc);
    ncplane_erase(p);

    int body = content_rows(a);
    for (int row = 0; row < body; row++) {
        int li = a->top + row;
        if (li >= (int)a->doc->nline) break;
        Line *L = &a->doc->lines[li];

        if (L->fill) {
            ncplane_set_fg_default(p);
            ncplane_set_bg_rgb8(p, L->fill_bg.r, L->fill_bg.g, L->fill_bg.b);
            ncplane_set_styles(p, NCSTYLE_NONE);
            for (int x = 0; x < a->cols; x++) ncplane_putchar_yx(p, row, x, ' ');
        }
        int x = 0;
        for (size_t j = 0; j < L->nrun && x < a->cols; j++) {
            apply_style(p, &L->runs[j].st);
            ncplane_putstr_yx(p, row, x, L->runs[j].text);
            x += run_cols(&L->runs[j]);
        }
    }

    int total = (int)a->doc->nline;
    int shown_end = a->top + body; if (shown_end > total) shown_end = total;
    int pct = total > 0 ? (shown_end * 100) / total : 100;
    char status[320];
    snprintf(status, sizeof status,
             " READER  %s  —  %d/%d (%d%%)   i insert   q quit   j/k scroll   g/G top/bottom",
             a->title ? a->title : "", total ? a->top + 1 : 0, total, pct);
    status_bar(a, status);
    notcurses_render(a->nc);
}

/* ---- Insert mode ---------------------------------------------------------- */

/* keep the cursor line and column on screen */
static void editor_scroll(App *a) {
    int body = content_rows(a);
    Editor *e = &a->ed;
    if (e->cy < e->top) e->top = e->cy;
    if (e->cy >= e->top + body) e->top = e->cy - body + 1;
    if (e->top < 0) e->top = 0;
    int ccol = editor_cursor_col(e);
    if (ccol < e->xoff) e->xoff = ccol;
    if (ccol >= e->xoff + a->cols) e->xoff = ccol - a->cols + 1;
    if (e->xoff < 0) e->xoff = 0;
}

static void draw_editor(App *a) {
    struct ncplane *p = a->plane;
    Editor *e = &a->ed;
    editor_scroll(a);
    ncplane_erase(p);
    ncplane_set_styles(p, NCSTYLE_NONE);
    ncplane_set_fg_default(p);
    ncplane_set_bg_default(p);

    int body = content_rows(a);
    for (int row = 0; row < body; row++) {
        size_t li = (size_t)e->top + row;
        if (li >= e->n) break;
        ELine *L = &e->lines[li];
        if (!L->b || L->len == 0) continue;
        size_t s = ed_col_to_byte(L->b, L->len, e->xoff);
        size_t en = ed_col_to_byte(L->b, L->len, e->xoff + a->cols);
        if (en <= s) continue;
        char save = L->b[en];           /* clip the visible window in place */
        L->b[en] = '\0';
        ncplane_putstr_yx(p, row, 0, L->b + s);
        L->b[en] = save;
    }

    char status[320];
    snprintf(status, sizeof status,
             " INSERT  %s%s  —  Ln %d, Col %d   Esc reader",
             a->title ? a->title : "", e->dirty ? " *" : "",
             e->cy + 1, editor_cursor_col(e) + 1);
    status_bar(a, status);

    notcurses_cursor_enable(a->nc, e->cy - e->top, editor_cursor_col(e) - e->xoff);
    notcurses_render(a->nc);
}

/* ---- mode transitions ----------------------------------------------------- */

static void update_dims(App *a) {
    unsigned r, c;
    ncplane_dim_yx(a->plane, &r, &c);
    a->rows = (int)r; a->cols = (int)c;
}

static int rerender(App *a) {
    update_dims(a);
    if (a->doc) doc_free(a->doc);
    a->doc = render_doc(a->src, a->srclen, a->cols, a->dark);
    if (!a->doc) return -1;
    clamp_top(a);
    return 0;
}

static void enter_insert(App *a) {
    editor_init(&a->ed, a->src, a->srclen);
    a->mode = MODE_INSERT;
}

static void leave_insert(App *a) {
    size_t nlen; char *ns = editor_source(&a->ed, &nlen);
    if (ns) { free(a->src); a->src = ns; a->srclen = nlen; }
    editor_free(&a->ed);
    a->mode = MODE_READER;
    rerender(a);
}

static void redraw(App *a) {
    if (a->mode == MODE_READER) draw_reader(a);
    else                        draw_editor(a);
}

/* ---- input ---------------------------------------------------------------- */

/* a printable text key: a real codepoint, not a control char or synthesized
 * special, and not a Ctrl-chord (those are commands). Alt/Option and Shift are
 * allowed on purpose — on a macOS layout Option composes characters (e.g. an
 * Italian keyboard types '#' as Option+3), so rejecting Alt drops them. */
static int is_text_key(uint32_t id, const ncinput *ni) {
    if (ncinput_ctrl_p(ni)) return 0;
    if (id < 0x20 || id == 0x7f || id > 0x10FFFF) return 0;
    return 1;
}

static int handle_reader(App *a, uint32_t id, const ncinput *ni) {
    int body = content_rows(a);
    if (id == 'q' || id == NCKEY_ESC)            return 0;   /* quit */
    else if (id == 'i')                          enter_insert(a);  /* vi-style */
    /* 'e' is reserved for Split view (slice 4) */
    else if (id == 'j' || id == NCKEY_DOWN)      a->top++;
    else if (id == 'k' || id == NCKEY_UP)        a->top--;
    else if (id == NCKEY_PGDOWN || (id == 'f' && ncinput_ctrl_p(ni))) a->top += body;
    else if (id == NCKEY_PGUP   || (id == 'b' && ncinput_ctrl_p(ni))) a->top -= body;
    else if (id == 'd' && ncinput_ctrl_p(ni))    a->top += body / 2;
    else if (id == 'u' && ncinput_ctrl_p(ni))    a->top -= body / 2;
    else if (id == 'g' || id == NCKEY_HOME)      a->top = 0;
    else if (id == 'G' || id == NCKEY_END)       a->top = (int)a->doc->nline;
    clamp_top(a);
    return 1;
}

static int handle_insert(App *a, uint32_t id, const ncinput *ni) {
    Editor *e = &a->ed;
    if (id == NCKEY_ESC) { leave_insert(a); return 1; }
    else if (id == NCKEY_ENTER || id == '\r' || id == '\n') editor_newline(e);
    else if (id == NCKEY_BACKSPACE || id == 0x08 || id == 0x7f) editor_backspace(e);
    else if (id == NCKEY_DEL)   editor_delete(e);
    else if (id == NCKEY_LEFT)  editor_left(e);
    else if (id == NCKEY_RIGHT) editor_right(e);
    else if (id == NCKEY_UP)    editor_up(e);
    else if (id == NCKEY_DOWN)  editor_down(e);
    else if (id == NCKEY_HOME)  editor_home(e);
    else if (id == NCKEY_END)   editor_end(e);
    else if (id == NCKEY_TAB || id == '\t') editor_insert(e, "    ", 4);
    else if (is_text_key(id, ni) && ni->utf8[0])
        editor_insert(e, ni->utf8, strlen(ni->utf8));
    return 1;
}

int tui_run(const char *src, unsigned long len, const char *title) {
    term_kbd_reset();   /* normalize before notcurses probes (slice-2 fix) */

    notcurses_options opts;
    memset(&opts, 0, sizeof opts);
    opts.flags = NCOPTION_SUPPRESS_BANNERS | NCOPTION_NO_QUIT_SIGHANDLERS;

    App a;
    memset(&a, 0, sizeof a);
    a.title = title; a.dark = 1; a.mode = MODE_READER;
    a.src = malloc(len + 1);
    if (!a.src) return 1;
    memcpy(a.src, src, len); a.src[len] = '\0'; a.srclen = len;

    a.nc = notcurses_init(&opts, NULL);
    if (!a.nc) { free(a.src); return 1; }
    a.plane = notcurses_stdplane(a.nc);

    g_nc = a.nc;
    signal(SIGINT,  on_fatal_signal);
    signal(SIGTERM, on_fatal_signal);
    signal(SIGSEGV, on_fatal_signal);
    signal(SIGABRT, on_fatal_signal);

    if (rerender(&a) != 0) { shutdown_tui(); free(a.src); return 1; }
    redraw(&a);

    ncinput ni;
    uint32_t id;
    int running = 1;
    while (running && (id = notcurses_get_blocking(a.nc, &ni)) != (uint32_t)-1) {
        if (ni.evtype == NCTYPE_RELEASE) continue;
        if (id == NCKEY_RESIZE) {
            if (a.mode == MODE_READER) rerender(&a);
            else update_dims(&a);
            redraw(&a);
            continue;
        }
        if (a.mode == MODE_READER) running = handle_reader(&a, id, &ni);
        else                       running = handle_insert(&a, id, &ni);
        if (running) redraw(&a);
    }

    /* drain anything the terminal already queued so leftover bytes don't spill
     * onto the shell prompt after we exit */
    ncinput drain; uint32_t d;
    while ((d = notcurses_get_nblock(a.nc, &drain)) != 0 && d != (uint32_t)-1) { }

    if (a.mode == MODE_INSERT) editor_free(&a.ed);
    doc_free(a.doc);
    shutdown_tui();
    free(a.src);
    return 0;
}
