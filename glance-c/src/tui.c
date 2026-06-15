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
    int    rcur_line, rcur_col;  /* Reader: block-cursor position (doc coords) */
    int    rcur_goal;            /* desired column for vertical cursor moves */
    int    rows, cols;
    int    dark;
    int    mode;
    Editor ed;
    int    cmdmode;              /* vi command line (':') active */
    char   cmdbuf[64];
    int    cmdlen;
} App;

static int content_rows(App *a) { return a->rows - 1; }   /* last row = status */

/* ---- terminal teardown (kitty keyboard protocol, see slice-2 fix) --------- */

static struct notcurses *g_nc = NULL;

/* Disable enhanced keyboard input modes. CSI < u pops the kitty keyboard stack;
 * CSI = 0 u forces all its flags off; CSI > 4 ; 0 m clears xterm modifyOtherKeys.
 * We call this right after notcurses_init (notcurses 3.0.17 enables the kitty
 * protocol but does not reliably disable it on iTerm2, leaking CSI-u sequences
 * to the shell). Running the whole session in legacy input mode sidesteps that:
 * the terminal delivers final composed characters directly, which is all an
 * editor needs. Also used at teardown as a belt-and-suspenders. Non-supporting
 * terminals ignore the unknown CSI. */
static void term_kbd_reset(void) {
    const char *seq = "\033[<u\033[=0u\033[>4;0m";
    ssize_t w = write(STDOUT_FILENO, seq, strlen(seq));
    (void)w;
}
static void shutdown_tui(void) {
    if (g_nc) { notcurses_stop(g_nc); g_nc = NULL; }
    term_kbd_reset();
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

/* keep the Reader cursor in range of the document and its line width */
static void reader_clamp_cursor(App *a) {
    if (a->doc->nline == 0) { a->rcur_line = a->rcur_col = 0; return; }
    if (a->rcur_line < 0) a->rcur_line = 0;
    if (a->rcur_line >= (int)a->doc->nline) a->rcur_line = (int)a->doc->nline - 1;
    int w = a->doc->lines[a->rcur_line].cols;
    if (a->rcur_col < 0) a->rcur_col = 0;
    if (a->rcur_col > w) a->rcur_col = w;
}

/* scroll the viewport so the Reader cursor line stays visible */
static void reader_scroll(App *a) {
    int body = content_rows(a);
    if (a->rcur_line < a->top) a->top = a->rcur_line;
    if (a->rcur_line >= a->top + body) a->top = a->rcur_line - body + 1;
    clamp_top(a);
}

/* draw a white block cursor over the cell at screen (y,x), keeping its glyph */
static void draw_block_cursor(App *a, int y, int x) {
    struct ncplane *p = a->plane;
    uint16_t sm; uint64_t ch;
    char *egc = ncplane_at_yx(p, y, x, &sm, &ch);
    ncplane_set_styles(p, NCSTYLE_NONE);
    ncplane_set_fg_rgb8(p, 0, 0, 0);
    ncplane_set_bg_rgb8(p, 255, 255, 255);
    ncplane_putstr_yx(p, y, x, (egc && egc[0] && egc[0] != ' ') ? egc : " ");
    free(egc);
}

/* draw the bottom line: the ':' command line when active, else the status */
static void reader_bottom(App *a) {
    if (a->cmdmode) {
        struct ncplane *p = a->plane;
        ncplane_set_styles(p, NCSTYLE_NONE);
        ncplane_set_fg_default(p);
        ncplane_set_bg_default(p);
        for (int x = 0; x < a->cols; x++) ncplane_putchar_yx(p, a->rows - 1, x, ' ');
        char cmd[80];
        snprintf(cmd, sizeof cmd, ":%s", a->cmdbuf);
        ncplane_putstr_yx(p, a->rows - 1, 0, cmd);
        return;
    }
    int total = (int)a->doc->nline;
    char status[320];
    snprintf(status, sizeof status,
             " READER  %s  —  Ln %d/%d, Col %d   i insert   :q quit   hjkl move",
             a->title ? a->title : "", a->rcur_line + 1, total, a->rcur_col + 1);
    status_bar(a, status);
}

static void draw_reader(App *a) {
    struct ncplane *p = a->plane;
    notcurses_cursor_disable(a->nc);
    reader_scroll(a);
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

    /* block cursor (only when not typing a command) */
    if (!a->cmdmode) {
        int cy = a->rcur_line - a->top;
        if (cy >= 0 && cy < body && a->rcur_col < a->cols)
            draw_block_cursor(a, cy, a->rcur_col);
    }

    reader_bottom(a);
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

/* Map a line index between the rendered Doc and the source proportionally.
 * md4c exposes no source offsets, so an exact rendered<->source line map isn't
 * available yet; this keeps the cursor roughly in place across mode switches
 * (the same approach the Go app uses). from in [0,nfrom), result in [0,nto). */
static int map_line(int from, int nfrom, int nto) {
    if (nfrom <= 1 || nto <= 1) return 0;
    int r = (from * (nto - 1)) / (nfrom - 1);
    if (r < 0) r = 0;
    if (r >= nto) r = nto - 1;
    return r;
}

static void enter_insert(App *a) {
    editor_init(&a->ed, a->src, a->srclen);
    int sl = map_line(a->rcur_line, (int)a->doc->nline, (int)a->ed.n);
    a->ed.cy = sl;
    ELine *L = &a->ed.lines[sl];
    a->ed.cx = (int)ed_col_to_byte(L->b ? L->b : "", L->len, a->rcur_col);
    a->ed.goal_col = -1;
    a->mode = MODE_INSERT;
}

static void leave_insert(App *a) {
    int src_cy = a->ed.cy, src_n = (int)a->ed.n;
    int rcol = editor_cursor_col(&a->ed);
    size_t nlen; char *ns = editor_source(&a->ed, &nlen);
    if (ns) { free(a->src); a->src = ns; a->srclen = nlen; }
    editor_free(&a->ed);
    a->mode = MODE_READER;
    rerender(a);
    a->rcur_line = map_line(src_cy, src_n, (int)a->doc->nline);
    a->rcur_col = rcol;
    a->rcur_goal = rcol;
    reader_clamp_cursor(a);
}

static void redraw(App *a) {
    if (a->mode == MODE_READER) draw_reader(a);
    else                        draw_editor(a);
}

/* ---- input ---------------------------------------------------------------- */

/* encode one Unicode codepoint as UTF-8; returns the byte count (1..4) */
static int cp_to_utf8(uint32_t cp, char *o) {
    if (cp < 0x80)    { o[0] = (char)cp; return 1; }
    if (cp < 0x800)   { o[0] = 0xC0 | (cp >> 6);  o[1] = 0x80 | (cp & 0x3F); return 2; }
    if (cp < 0x10000) { o[0] = 0xE0 | (cp >> 12); o[1] = 0x80 | ((cp >> 6) & 0x3F);
                        o[2] = 0x80 | (cp & 0x3F); return 3; }
    o[0] = 0xF0 | (cp >> 18); o[1] = 0x80 | ((cp >> 12) & 0x3F);
    o[2] = 0x80 | ((cp >> 6) & 0x3F); o[3] = 0x80 | (cp & 0x3F); return 4;
}

/* Insert the key's *effective* text — the character the user actually meant,
 * with layout/modifier composition applied. The kitty keyboard protocol reports
 * the physical key in id/utf8 (e.g. '3' for Option+3, '+' for Shift+'+') and the
 * composed result in eff_text (e.g. '#', '*'), so eff_text is the right source.
 * Falls back to utf8 when eff_text is empty. Ctrl-chords are commands, not text.
 * Returns 1 if something was inserted. */
static int try_insert_text(Editor *e, const ncinput *ni) {
    if (ncinput_ctrl_p(ni)) return 0;
    char buf[NCINPUT_MAX_EFF_TEXT_CODEPOINTS * 4];
    int len = 0;
    for (int i = 0; i < NCINPUT_MAX_EFF_TEXT_CODEPOINTS && ni->eff_text[i]; i++) {
        uint32_t cp = ni->eff_text[i];
        if (cp < 0x20 || cp == 0x7f || cp > 0x10FFFF) continue;
        len += cp_to_utf8(cp, buf + len);
    }
    if (len == 0 && ni->utf8[0] && (unsigned char)ni->utf8[0] >= 0x20 &&
        (unsigned char)ni->utf8[0] != 0x7f && ni->id <= 0x10FFFF) {
        len = (int)strlen(ni->utf8);
        memcpy(buf, ni->utf8, len);
    }
    if (len == 0) return 0;
    editor_insert(e, buf, len);
    return 1;
}

/* execute a typed ':' command. Returns 0 to quit, 1 to keep running. */
static int run_command(App *a) {
    const char *c = a->cmdbuf;
    a->cmdmode = 0;
    if (!strcmp(c, "q") || !strcmp(c, "q!") || !strcmp(c, "quit")) return 0;
    /* :w / :wq save — deferred to slice 5; for now no-op, keep running */
    a->cmdbuf[0] = '\0'; a->cmdlen = 0;
    return 1;
}

static int handle_command(App *a, uint32_t id, const ncinput *ni) {
    if (id == NCKEY_ESC) { a->cmdmode = 0; a->cmdbuf[0] = '\0'; a->cmdlen = 0; return 1; }
    if (id == NCKEY_ENTER || id == '\r' || id == '\n') return run_command(a);
    if (id == NCKEY_BACKSPACE || id == 0x08 || id == 0x7f) {
        if (a->cmdlen > 0) a->cmdbuf[--a->cmdlen] = '\0';
        else a->cmdmode = 0;            /* backspace past ':' exits command mode */
        return 1;
    }
    /* command text is ASCII (q, w, ...); take the effective codepoint */
    uint32_t cp = ni->eff_text[0] ? ni->eff_text[0] : id;
    if (!ncinput_ctrl_p(ni) && cp >= 0x20 && cp < 0x7f &&
        a->cmdlen < (int)sizeof(a->cmdbuf) - 1) {
        a->cmdbuf[a->cmdlen++] = (char)cp;
        a->cmdbuf[a->cmdlen] = '\0';
    }
    return 1;
}

static int handle_reader(App *a, uint32_t id, const ncinput *ni) {
    int body = content_rows(a);
    if (id == 'c' && ncinput_ctrl_p(ni))         return 0;   /* escape hatch */
    else if (id == ':')                          { a->cmdmode = 1; a->cmdbuf[0] = '\0'; a->cmdlen = 0; }
    else if (id == 'i')                          enter_insert(a);  /* vi-style */
    /* 'e' is reserved for Split view (slice 4) */
    else if (id == 'j' || id == NCKEY_DOWN)      { a->rcur_line++; a->rcur_col = a->rcur_goal; }
    else if (id == 'k' || id == NCKEY_UP)        { a->rcur_line--; a->rcur_col = a->rcur_goal; }
    else if (id == 'h' || id == NCKEY_LEFT)      { a->rcur_col--; a->rcur_goal = a->rcur_col; }
    else if (id == 'l' || id == NCKEY_RIGHT)     { a->rcur_col++; a->rcur_goal = a->rcur_col; }
    else if (id == NCKEY_PGDOWN || (id == 'f' && ncinput_ctrl_p(ni))) { a->rcur_line += body; a->rcur_col = a->rcur_goal; }
    else if (id == NCKEY_PGUP   || (id == 'b' && ncinput_ctrl_p(ni))) { a->rcur_line -= body; a->rcur_col = a->rcur_goal; }
    else if (id == 'd' && ncinput_ctrl_p(ni))    { a->rcur_line += body / 2; a->rcur_col = a->rcur_goal; }
    else if (id == 'u' && ncinput_ctrl_p(ni))    { a->rcur_line -= body / 2; a->rcur_col = a->rcur_goal; }
    else if (id == 'g' || id == NCKEY_HOME)      a->rcur_line = 0;
    else if (id == 'G' || id == NCKEY_END)       a->rcur_line = (int)a->doc->nline - 1;
    reader_clamp_cursor(a);
    if (id == 'h' || id == 'l' || id == NCKEY_LEFT || id == NCKEY_RIGHT)
        a->rcur_goal = a->rcur_col;     /* horizontal move resets the goal col */
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
    else try_insert_text(e, ni);
    return 1;
}

int tui_keyprobe(void) {
    term_kbd_reset();
    notcurses_options opts;
    memset(&opts, 0, sizeof opts);
    opts.flags = NCOPTION_SUPPRESS_BANNERS;
    struct notcurses *nc = notcurses_init(&opts, NULL);
    if (!nc) return 1;
    g_nc = nc;
    term_kbd_reset();              /* legacy keyboard mode (see term_kbd_reset) */
    atexit(term_kbd_reset);

    struct ncplane *p = notcurses_stdplane(nc);
    unsigned rows, cols; ncplane_dim_yx(p, &rows, &cols);
    const char *hdr = " key probe — press keys (try Option+3, etc).  Esc to quit ";
    ncplane_erase(p);
    ncplane_putstr_yx(p, 0, 0, hdr);
    notcurses_render(nc);

    int line = 2;
    ncinput ni; uint32_t id;
    while ((id = notcurses_get_blocking(nc, &ni)) != (uint32_t)-1) {
        if (ni.evtype == NCTYPE_RELEASE) continue;
        /* Esc, or Ctrl+C delivered as a key (raw mode swallows the signal),
         * quits through the normal clean path — no flow-control traps. */
        if (id == NCKEY_ESC || (id == 'c' && ncinput_ctrl_p(&ni))) break;

        char hex[64] = {0};
        int hp = 0;
        for (int i = 0; ni.utf8[i] && i < 4; i++)
            hp += snprintf(hex + hp, sizeof hex - hp, "%02x ", (unsigned char)ni.utf8[i]);

        char eff[64] = {0};
        int ep = 0;
        for (int i = 0; i < NCINPUT_MAX_EFF_TEXT_CODEPOINTS && ni.eff_text[i]; i++)
            ep += snprintf(eff + ep, sizeof eff - ep, "U+%04X ", ni.eff_text[i]);

        char buf[256];
        snprintf(buf, sizeof buf,
                 "id=0x%06X  utf8=\"%s\"  hex=[%s]  eff=[%s]  alt=%d shift=%d ctrl=%d",
                 id, ni.utf8, hex, eff,
                 ncinput_alt_p(&ni) ? 1 : 0, ncinput_shift_p(&ni) ? 1 : 0,
                 ncinput_ctrl_p(&ni) ? 1 : 0);

        if (line >= (int)rows - 1) {
            line = 2;
            ncplane_erase(p);
            ncplane_putstr_yx(p, 0, 0, hdr);
        }
        ncplane_putstr_yx(p, line++, 0, buf);
        notcurses_render(nc);
    }

    ncinput d; uint32_t dd;
    while ((dd = notcurses_get_nblock(nc, &d)) != 0 && dd != (uint32_t)-1) { }
    shutdown_tui();
    return 0;
}

int tui_run(const char *src, unsigned long len, const char *title) {
    term_kbd_reset();   /* normalize before notcurses probes (slice-2 fix) */

    notcurses_options opts;
    memset(&opts, 0, sizeof opts);
    opts.flags = NCOPTION_SUPPRESS_BANNERS;   /* let notcurses own signal cleanup */

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
    term_kbd_reset();              /* run in legacy keyboard mode (see above) */
    atexit(term_kbd_reset);        /* and restore on any normal exit path */

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
        if (a.cmdmode)             running = handle_command(&a, id, &ni);
        else if (a.mode == MODE_READER) running = handle_reader(&a, id, &ni);
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
