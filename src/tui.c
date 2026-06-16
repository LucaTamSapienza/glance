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
#include "search.h"
#include "toc.h"
#include "fs_save.h"
#include "fswatch.h"
#include "clipboard.h"
#include "completion.h"
#include "vault.h"
#include "graph.h"
#include "util.h"

#include <notcurses/notcurses.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <errno.h>
#include <poll.h>
#include <libgen.h>
#include <dirent.h>
#include <unistd.h>

enum { MODE_READER, MODE_INSERT, MODE_SPLIT };

typedef struct {
    struct notcurses *nc;
    struct ncplane   *plane;
    char  *src; size_t srclen;   /* owned, mutable copy of the document */
    char   title[256];           /* status-bar name (file base name or "stdin") */
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
    int    searchmode;           /* '/' search prompt active */
    char   searchbuf[128];
    int    searchlen;
    Hits   hits;                 /* current search matches */
    int    cur_hit;              /* index of the focused match, or -1 */
    int    tocmode;              /* table-of-contents panel open */
    TOC    toc;
    int    toc_sel;              /* selected TOC entry */
    char  *path;                 /* current file (owned), or NULL for stdin */
    char  *back[64];             /* navigation back-stack of file paths (owned) */
    int    nback;
    Watch  watch;                /* file watcher for the current file's dir */
    int    wfd;                  /* watcher kqueue fd, or -1 */
    int    dirty;                /* unsaved changes since the last write */
    char   msg[160];             /* transient status message (one keypress) */
    Doc   *preview;              /* Split mode: live render of the editor text */
    int    helpmode;             /* help overlay open */
    int    visualmode;           /* vi visual-line selection active */
    int    visual_anchor;        /* line where the selection started */
    int    blmode;               /* backlinks panel open */
    char  *bl[256];              /* file names that link here (owned) */
    int    nbl, bl_sel;
    int    graphmode;            /* interactive graph explorer open */
    Graph  graph;                /* the vault link graph */
    char   graph_root[4096];     /* vault root the graph was built from */
    int    graph_focus;          /* node at the centre */
    int    graph_sel;            /* selected neighbour (backlinks then outbound) */
    struct ncplane *imgpl[16];   /* planes the reader blits decoded images onto */
    int    nimgpl;
} App;

#define TOC_W 30                 /* table-of-contents panel width */

static int content_rows(App *a) { return a->rows - 1; }   /* last row = status */

static int map_line(int from, int nfrom, int nto);        /* defined below */
static const char *doc_dir(App *a, char *buf, size_t cap);/* defined below */
static int doc_src_line(const Doc *d, int line);          /* defined below */
static int src_doc_line(const Doc *d, int srcline);       /* defined below */
static void open_graph(App *a);                           /* defined below */

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
/* Decide dark vs light from the terminal's reported background colour. Falls
 * back to dark when the terminal doesn't tell us (the common, safe default). */
static int detect_dark(struct notcurses *nc) {
    uint32_t bg;
    if (notcurses_default_background(nc, &bg) != 0) return 1;
    int r = (bg >> 16) & 0xff, g = (bg >> 8) & 0xff, b = bg & 0xff;
    int luma = (299 * r + 587 * g + 114 * b) / 1000;   /* perceived brightness */
    return luma < 128;
}

/* Tear down notcurses and restore the terminal's keyboard state. */
static void shutdown_tui(void) {
    if (g_nc) { notcurses_stop(g_nc); g_nc = NULL; }
    term_kbd_reset();
}

/* ---- shared helpers ------------------------------------------------------- */

/* Apply a Style to plane p: emphasis flags plus fg/bg (default when unset). */
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

/* Draw `text` on the bottom row as a highlighted status bar. */
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

/* Keep the scroll offset within [0, last full page]. */
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

/* Recolour the cell at screen (y,x) to fg-on-bg, preserving its glyph. Used to
 * overlay the block cursor and search highlights on top of rendered content. */
static void overlay_cell(struct ncplane *p, int y, int x, RGB fg, RGB bg) {
    uint16_t sm; uint64_t ch;
    char *egc = ncplane_at_yx(p, y, x, &sm, &ch);
    ncplane_set_styles(p, NCSTYLE_NONE);
    ncplane_set_fg_rgb8(p, fg.r, fg.g, fg.b);
    ncplane_set_bg_rgb8(p, bg.r, bg.g, bg.b);
    ncplane_putstr_yx(p, y, x, (egc && egc[0] && egc[0] != ' ') ? egc : " ");
    free(egc);
}

/* Tint the visual-line selection (from the anchor to the cursor line). */
static void draw_selection(App *a) {
    if (!a->visualmode) return;
    int lo = a->visual_anchor, hi = a->rcur_line;
    if (lo > hi) { int t = lo; lo = hi; hi = t; }
    RGB selfg = {235, 235, 235}, selbg = a->dark ? (RGB){50, 60, 90} : (RGB){190, 205, 235};
    int body = content_rows(a);
    for (int li = lo; li <= hi; li++) {
        int y = li - a->top;
        if (y < 0 || y >= body) continue;
        for (int x = 0; x < a->cols; x++) overlay_cell(a->plane, y, x, selfg, selbg);
    }
}

/* Highlight every visible search hit (yellow), brighter for the focused one. */
static void draw_hits(App *a) {
    int body = content_rows(a);
    RGB black = {0, 0, 0}, yellow = {220, 200, 60}, amber = {255, 170, 40};
    for (int i = 0; i < a->hits.n; i++) {
        Hit *h = &a->hits.v[i];
        int y = h->line - a->top;
        if (y < 0 || y >= body) continue;
        RGB bg = (i == a->cur_hit) ? amber : yellow;
        for (int c = 0; c < h->width && h->col + c < a->cols; c++)
            overlay_cell(a->plane, y, h->col + c, black, bg);
    }
}

/* Draw the bottom row as a plain prompt line (for ':' and '/'). */
static void prompt_line(App *a, char lead, const char *text) {
    struct ncplane *p = a->plane;
    ncplane_set_styles(p, NCSTYLE_NONE);
    ncplane_set_fg_default(p);
    ncplane_set_bg_default(p);
    for (int x = 0; x < a->cols; x++) ncplane_putchar_yx(p, a->rows - 1, x, ' ');
    char line[160];
    snprintf(line, sizeof line, "%c%s", lead, text);
    ncplane_putstr_yx(p, a->rows - 1, 0, line);
}

/* Draw the bottom line: an active ':'/'/' prompt, a transient message, else the
 * Reader status. */
static void reader_bottom(App *a) {
    if (a->cmdmode)    { prompt_line(a, ':', a->cmdbuf); return; }
    if (a->searchmode) { prompt_line(a, '/', a->searchbuf); return; }
    if (a->msg[0])     { status_bar(a, a->msg); return; }
    if (a->visualmode) {
        int n = abs(a->rcur_line - a->visual_anchor) + 1;
        char vs[160];
        snprintf(vs, sizeof vs, " VISUAL  %d line%s  —  j/k extend   y yank   Esc cancel",
                 n, n == 1 ? "" : "s");
        status_bar(a, vs);
        return;
    }
    char status[320];
    if (a->hits.n > 0) {
        snprintf(status, sizeof status,
                 " READER  %s  —  match %d/%d for \"%s\"   n/N next/prev   Esc clear",
                 a->title, a->cur_hit + 1, a->hits.n, a->searchbuf);
    } else {
        snprintf(status, sizeof status,
                 " READER  %s  —  Ln %d/%d   i insert  e split  / search  t toc  :q quit",
                 a->title, a->rcur_line + 1, (int)a->doc->nline);
    }
    status_bar(a, status);
}

/* Draw the table-of-contents panel over the left edge of the view. */
static void draw_toc(App *a) {
    struct ncplane *p = a->plane;
    int h = content_rows(a);
    RGB panel = a->dark ? (RGB){30, 30, 38} : (RGB){235, 235, 240};
    RGB fg    = a->dark ? (RGB){210, 210, 210} : (RGB){30, 30, 30};
    int first = a->toc_sel - h / 2 + 1;        /* scroll so the selection shows */
    if (first > a->toc.n - (h - 1)) first = a->toc.n - (h - 1);
    if (first < 0) first = 0;

    ncplane_set_styles(p, NCSTYLE_BOLD);
    ncplane_set_fg_rgb8(p, fg.r, fg.g, fg.b);
    ncplane_set_bg_rgb8(p, panel.r, panel.g, panel.b);
    for (int x = 0; x < TOC_W; x++) ncplane_putchar_yx(p, 0, x, ' ');
    ncplane_putstr_yx(p, 0, 1, "Contents");

    for (int row = 1; row < h; row++) {
        int idx = first + row - 1;
        for (int x = 0; x < TOC_W; x++) ncplane_putchar_yx(p, row, x, ' ');
        if (idx < 0 || idx >= a->toc.n) continue;
        TOCItem *it = &a->toc.v[idx];
        int sel = (idx == a->toc_sel);
        ncplane_set_styles(p, sel ? NCSTYLE_BOLD : NCSTYLE_NONE);
        if (sel) { ncplane_set_fg_rgb8(p, 0, 0, 0); ncplane_set_bg_rgb8(p, 255, 200, 90); }
        else     { ncplane_set_fg_rgb8(p, fg.r, fg.g, fg.b); ncplane_set_bg_rgb8(p, panel.r, panel.g, panel.b); }
        char buf[TOC_W * 4];
        int indent = (it->level - 1) * 2;
        snprintf(buf, sizeof buf, "%*s%s", indent + 1, "", it->title);
        size_t cut = ed_col_to_byte(buf, strlen(buf), TOC_W - 1);   /* clip to panel */
        buf[cut] = '\0';
        ncplane_putstr_yx(p, row, 0, buf);
    }
    ncplane_set_styles(p, NCSTYLE_NONE);
    ncplane_set_fg_default(p);
    ncplane_set_bg_default(p);
}

/* Draw one rendered Doc line at screen (row, x0): a code/quote background fill
 * across `width`, then the styled runs. The Doc must have been rendered at the
 * pane width, so runs already fit and need no clipping. */
static void draw_doc_line(struct ncplane *p, const Line *L, int row, int x0, int width) {
    if (L->fill) {
        ncplane_set_fg_default(p);
        ncplane_set_bg_rgb8(p, L->fill_bg.r, L->fill_bg.g, L->fill_bg.b);
        ncplane_set_styles(p, NCSTYLE_NONE);
        for (int x = 0; x < width; x++) ncplane_putchar_yx(p, row, x0 + x, ' ');
    }
    int x = 0;
    for (size_t j = 0; j < L->nrun && x < width; j++) {
        apply_style(p, &L->runs[j].st);
        ncplane_putstr_yx(p, row, x0 + x, L->runs[j].text);
        x += u8_width(L->runs[j].text, L->runs[j].len);
    }
}

/* Draw the backlinks panel: files that link to the current one. */
static void draw_backlinks(App *a) {
    struct ncplane *p = a->plane;
    int h = content_rows(a);
    RGB panel = a->dark ? (RGB){30, 30, 38} : (RGB){235, 235, 240};
    RGB fg    = a->dark ? (RGB){210, 210, 210} : (RGB){30, 30, 30};
    ncplane_set_styles(p, NCSTYLE_BOLD);
    ncplane_set_fg_rgb8(p, fg.r, fg.g, fg.b);
    ncplane_set_bg_rgb8(p, panel.r, panel.g, panel.b);
    for (int x = 0; x < TOC_W; x++) ncplane_putchar_yx(p, 0, x, ' ');
    ncplane_putstr_yx(p, 0, 1, "Backlinks");
    for (int row = 1; row < h; row++) {
        int idx = row - 1;
        for (int x = 0; x < TOC_W; x++) ncplane_putchar_yx(p, row, x, ' ');
        if (idx >= a->nbl) continue;
        int sel = (idx == a->bl_sel);
        ncplane_set_styles(p, sel ? NCSTYLE_BOLD : NCSTYLE_NONE);
        if (sel) { ncplane_set_fg_rgb8(p, 0, 0, 0); ncplane_set_bg_rgb8(p, 255, 200, 90); }
        else     { ncplane_set_fg_rgb8(p, fg.r, fg.g, fg.b); ncplane_set_bg_rgb8(p, panel.r, panel.g, panel.b); }
        const char *slash = strrchr(a->bl[idx], '/');   /* show the base name */
        char buf[TOC_W * 4];
        snprintf(buf, sizeof buf, " %s", slash ? slash + 1 : a->bl[idx]);
        size_t cut = ed_col_to_byte(buf, strlen(buf), TOC_W - 1);
        buf[cut] = '\0';
        ncplane_putstr_yx(p, row, 0, buf);
    }
    ncplane_set_styles(p, NCSTYLE_NONE);
    ncplane_set_fg_default(p);
    ncplane_set_bg_default(p);
}

/* The base name of a node's relative path. */
static const char *node_base(const char *rel) {
    const char *s = strrchr(rel, '/');
    return s ? s + 1 : rel;
}

/* Collect the deduplicated backlink and outbound neighbours of the focus node. */
static void graph_neighbors(App *a, int *back, int *nb, int *out, int *no) {
    *nb = *no = 0;
    int f = a->graph_focus;
    for (int e = 0; e < a->graph.ne; e++) {
        GEdge *ed = &a->graph.edge[e];
        if (ed->from == f && ed->to != f) {
            int dup = 0; for (int i = 0; i < *no; i++) if (out[i] == ed->to) dup = 1;
            if (!dup && *no < 256) out[(*no)++] = ed->to;
        }
        if (ed->to == f && ed->from != f) {
            int dup = 0; for (int i = 0; i < *nb; i++) if (back[i] == ed->from) dup = 1;
            if (!dup && *nb < 256) back[(*nb)++] = ed->from;
        }
    }
}

/* Draw one neighbour name, highlighting it when selected. */
static void graph_name(App *a, int y, int x, const char *name, int selected) {
    struct ncplane *p = a->plane;
    if (selected) { ncplane_set_styles(p, NCSTYLE_BOLD); ncplane_set_fg_rgb8(p, 0, 0, 0); ncplane_set_bg_rgb8(p, 255, 200, 90); }
    else { ncplane_set_styles(p, NCSTYLE_NONE); ncplane_set_fg_rgb8(p, 200, 200, 200); ncplane_set_bg_default(p); }
    ncplane_putstr_yx(p, y, x, name);
}

/* The interactive graph explorer: the focused note in the centre, the notes
 * that link to it on the left, the notes it links to on the right. */
static void draw_graph(App *a) {
    struct ncplane *p = a->plane;
    ncplane_set_styles(p, NCSTYLE_NONE);
    ncplane_set_fg_default(p); ncplane_set_bg_default(p);
    ncplane_erase(p);

    int back[256], out[256], nb, no;
    graph_neighbors(a, back, &nb, out, &no);

    int midy = a->rows / 2;
    int leftx = 2, rightx = a->cols * 2 / 3;
    int centerx = a->cols / 3 + 2;

    /* backlinks down the left, each with an arrow pointing at the focus */
    for (int i = 0; i < nb; i++) {
        int y = midy - nb / 2 + i;
        if (y < 1 || y >= a->rows - 1) continue;
        graph_name(a, y, leftx, node_base(a->graph.node[back[i]]), a->graph_sel == i);
        ncplane_set_styles(p, NCSTYLE_NONE);
        ncplane_set_fg_rgb8(p, 110, 110, 110); ncplane_set_bg_default(p);
        ncplane_putstr_yx(p, y, centerx - 4, "──→");
    }
    /* outbound on the right, the focus pointing at each */
    for (int i = 0; i < no; i++) {
        int y = midy - no / 2 + i;
        if (y < 1 || y >= a->rows - 1) continue;
        ncplane_set_styles(p, NCSTYLE_NONE);
        ncplane_set_fg_rgb8(p, 110, 110, 110); ncplane_set_bg_default(p);
        ncplane_putstr_yx(p, y, centerx + 14, "──→");
        graph_name(a, y, rightx, node_base(a->graph.node[out[i]]), a->graph_sel == nb + i);
    }
    /* the focus node, boxed in the centre */
    ncplane_set_styles(p, NCSTYLE_BOLD);
    ncplane_set_fg_rgb8(p, 20, 20, 20);
    ncplane_set_bg_rgb8(p, 120, 200, 255);
    char box[64];
    snprintf(box, sizeof box, " %s ", node_base(a->graph.node[a->graph_focus]));
    ncplane_putstr_yx(p, midy, centerx, box);

    if (nb == 0 && no == 0) {
        ncplane_set_styles(p, NCSTYLE_NONE);
        ncplane_set_fg_rgb8(p, 150, 150, 150); ncplane_set_bg_default(p);
        ncplane_putstr_yx(p, midy + 2, centerx, "(this note has no links)");
    }

    char title[256];
    snprintf(title, sizeof title,
             " GRAPH  %d in / %d out   —  arrows/hjkl move   Enter open   Space recenter   Esc close",
             nb, no);
    status_bar(a, title);
    notcurses_render(a->nc);
}

/* Render the visible Doc lines, overlay the block cursor, and the status. */
/* Destroy the image planes from the previous frame. */
static void clear_images(App *a) {
    for (int i = 0; i < a->nimgpl; i++) ncplane_destroy(a->imgpl[i]);
    a->nimgpl = 0;
}

/* Blit an image onto a child plane covering the reserved rows. The visual is
 * decoded fresh each frame: reusing one ncvisual across frames confuses
 * notcurses' pixel-sprite bookkeeping and leaks escape sequences to the screen.
 * Anything that can't be drawn (a URL, a missing file, a terminal without image
 * support) silently leaves the ▦ placeholder text the renderer already drew. */
static void blit_image(App *a, const char *src, int row, int rows) {
    if (a->nimgpl >= (int)(sizeof a->imgpl / sizeof a->imgpl[0])) return;
    int body = content_rows(a);
    int h = rows; if (row + h > body) h = body - row;
    int w = a->cols;
    if (h < 1 || w < 2) return;
    char dirbuf[4096];
    char *path = path_resolve(doc_dir(a, dirbuf, sizeof dirbuf), src);
    if (!path) return;
    struct ncvisual *v = ncvisual_from_file(path);
    free(path);
    if (!v) return;
    /* Wipe the placeholder text from the reserved rows so it can't show through
     * the letterbox margins NCSCALE_SCALE leaves around the picture. */
    ncplane_set_fg_default(a->plane);
    ncplane_set_bg_default(a->plane);
    for (int ry = row; ry < row + h; ry++)
        for (int rx = 0; rx < w; rx++) ncplane_putchar_yx(a->plane, ry, rx, ' ');
    /* Cover the full row width (from column 0); NCSCALE_SCALE letterboxes the
     * picture within the box. */
    struct ncplane_options no; memset(&no, 0, sizeof no);
    no.y = row; no.x = 0; no.rows = (unsigned)h; no.cols = (unsigned)w;
    struct ncplane *ip = ncplane_create(a->plane, &no);
    if (!ip) { ncvisual_destroy(v); return; }
    struct ncvisual_options vo; memset(&vo, 0, sizeof vo);
    vo.n = ip; vo.scaling = NCSCALE_SCALE; vo.blitter = NCBLIT_DEFAULT;
    if (ncvisual_blit(a->nc, v, &vo) == NULL) ncplane_destroy(ip);
    else a->imgpl[a->nimgpl++] = ip;
    ncvisual_destroy(v);
}

static void draw_reader(App *a) {
    notcurses_cursor_disable(a->nc);
    reader_scroll(a);
    ncplane_erase(a->plane);

    int body = content_rows(a);
    for (int row = 0; row < body; row++) {
        int li = a->top + row;
        if (li >= (int)a->doc->nline) break;
        draw_doc_line(a->plane, &a->doc->lines[li], row, 0, a->cols);
    }

    draw_selection(a);
    draw_hits(a);

    /* block cursor on top (hidden while typing a command/search/panel) */
    if (!a->cmdmode && !a->searchmode && !a->tocmode && !a->blmode) {
        int cy = a->rcur_line - a->top;
        RGB black = {0, 0, 0}, white = {255, 255, 255};
        if (cy >= 0 && cy < body && a->rcur_col < a->cols)
            overlay_cell(a->plane, cy, a->rcur_col, black, white);
    }

    /* Blit any visible images over their reserved rows (not while a panel or
     * prompt is open, which would draw over the picture). */
    if (!a->tocmode && !a->blmode && !a->helpmode && !a->cmdmode && !a->searchmode) {
        for (int row = 0; row < body; row++) {
            int li = a->top + row;
            if (li >= (int)a->doc->nline) break;
            Line *L = &a->doc->lines[li];
            if (L->image && L->img_rows > 0) blit_image(a, L->image, row, L->img_rows);
        }
    }

    if (a->tocmode) draw_toc(a);
    if (a->blmode)  draw_backlinks(a);
    reader_bottom(a);
    notcurses_render(a->nc);
}

/* ---- Insert / Split editing ---------------------------------------------- */

/* Keep the editor cursor visible within a pane `width` columns wide. */
static void editor_scroll(App *a, int width) {
    int body = content_rows(a);
    Editor *e = &a->ed;
    if (e->cy < e->top) e->top = e->cy;
    if (e->cy >= e->top + body) e->top = e->cy - body + 1;
    if (e->top < 0) e->top = 0;
    int ccol = editor_cursor_col(e);
    if (ccol < e->xoff) e->xoff = ccol;
    if (ccol >= e->xoff + width) e->xoff = ccol - width + 1;
    if (e->xoff < 0) e->xoff = 0;
}

/* Draw the editor's visible lines into a pane at x0, clipped to `width`. */
static void draw_editor_pane(App *a, int x0, int width) {
    struct ncplane *p = a->plane;
    Editor *e = &a->ed;
    int body = content_rows(a);
    ncplane_set_styles(p, NCSTYLE_NONE);
    ncplane_set_fg_default(p);
    ncplane_set_bg_default(p);
    for (int row = 0; row < body; row++) {
        size_t li = (size_t)e->top + row;
        if (li >= e->n) break;
        ELine *L = &e->lines[li];
        if (!L->b || L->len == 0) continue;
        size_t s = ed_col_to_byte(L->b, L->len, e->xoff);
        size_t en = ed_col_to_byte(L->b, L->len, e->xoff + width);
        if (en <= s) continue;
        char save = L->b[en];           /* clip the visible window in place */
        L->b[en] = '\0';
        ncplane_putstr_yx(p, row, x0, L->b + s);
        L->b[en] = save;
    }
}

/* Full-screen editor (Insert mode): pane + status + hardware cursor. */
static void draw_editor(App *a) {
    Editor *e = &a->ed;
    editor_scroll(a, a->cols);
    ncplane_erase(a->plane);
    draw_editor_pane(a, 0, a->cols);

    char status[320];
    if (a->msg[0]) snprintf(status, sizeof status, " %s", a->msg);
    else snprintf(status, sizeof status,
                  " INSERT  %s%s  —  Ln %d, Col %d   Esc reader   ^S save",
                  a->title, e->dirty ? " *" : "",
                  e->cy + 1, editor_cursor_col(e) + 1);
    status_bar(a, status);

    notcurses_cursor_enable(a->nc, e->cy - e->top, editor_cursor_col(e) - e->xoff);
    notcurses_render(a->nc);
}

/* The document's directory (for resolving relative image paths), into buf, or
 * NULL for stdin input that has no path. */
static const char *doc_dir(App *a, char *buf, size_t cap) {
    if (!a->path) return NULL;
    snprintf(buf, cap, "%s", a->path);
    return dirname(buf);
}

/* Re-render the live preview from the editor text at the right-pane width. */
static void render_preview(App *a) {
    if (a->preview) { doc_free(a->preview); a->preview = NULL; }
    int rw = a->cols - a->cols / 2 - 1;
    if (rw < 4) rw = 4;
    char dirbuf[4096];
    const char *base = doc_dir(a, dirbuf, sizeof dirbuf);
    size_t nlen; char *s = editor_source(&a->ed, &nlen);
    if (s) { a->preview = render_doc_at(s, nlen, rw, a->dark, base); free(s); }
}

/* Split mode: editor on the left, a live preview on the right, a divider
 * between. The preview scrolls to keep the editor's current line centred. */
static void draw_split(App *a) {
    struct ncplane *p = a->plane;
    Editor *e = &a->ed;
    int leftw = a->cols / 2, rightw = a->cols - leftw - 1, body = content_rows(a);
    editor_scroll(a, leftw);
    ncplane_erase(p);
    draw_editor_pane(a, 0, leftw);

    /* divider */
    RGB d = a->dark ? (RGB){80, 80, 80} : (RGB){170, 170, 170};
    ncplane_set_styles(p, NCSTYLE_NONE);
    ncplane_set_fg_rgb8(p, d.r, d.g, d.b);
    ncplane_set_bg_default(p);
    for (int row = 0; row < body; row++) ncplane_putstr_yx(p, row, leftw, "\xe2\x94\x82");

    /* preview, centred on the line that matches the editor cursor */
    if (a->preview) {
        int focus = src_doc_line(a->preview, e->cy);
        if (focus < 0) focus = map_line(e->cy, (int)e->n, (int)a->preview->nline);
        int ptop = focus - body / 2;
        if (ptop > (int)a->preview->nline - body) ptop = (int)a->preview->nline - body;
        if (ptop < 0) ptop = 0;
        for (int row = 0; row < body; row++) {
            int pli = ptop + row;
            if (pli >= (int)a->preview->nline) break;
            draw_doc_line(p, &a->preview->lines[pli], row, leftw + 1, rightw);
        }
    }

    char status[320];
    if (a->msg[0]) snprintf(status, sizeof status, " %s", a->msg);
    else snprintf(status, sizeof status,
                  " SPLIT  %s%s  —  edit left, preview right   Esc reader   ^S save",
                  a->title, e->dirty ? " *" : "");
    status_bar(a, status);

    notcurses_cursor_enable(a->nc, e->cy - e->top, editor_cursor_col(e) - e->xoff);
    notcurses_render(a->nc);
}

/* ---- mode transitions ----------------------------------------------------- */

/* Refresh the cached terminal size from the plane. */
static void update_dims(App *a) {
    unsigned r, c;
    ncplane_dim_yx(a->plane, &r, &c);
    a->rows = (int)r; a->cols = (int)c;
}

/* Re-render the document at the current width (after edits or a resize). Any
 * live search is recomputed, since hit line indices refer to the old Doc. */
static int rerender(App *a) {
    update_dims(a);
    if (a->doc) doc_free(a->doc);
    char dirbuf[4096];
    const char *base = doc_dir(a, dirbuf, sizeof dirbuf);
    a->doc = render_doc_at(a->src, a->srclen, a->cols, a->dark, base);
    if (!a->doc) return -1;
    if (a->searchbuf[0]) {
        search_doc(a->doc, a->searchbuf, &a->hits);
        if (a->cur_hit >= a->hits.n) a->cur_hit = a->hits.n - 1;
    }
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

/* 0-based source line a rendered Doc line came from (nearest tagged line at or
 * above it), or -1 if the Doc carries no source tags. render.c fills the tags
 * by content matching; this turns them into an exact reader->editor map. */
static int doc_src_line(const Doc *d, int line) {
    for (int i = line; i >= 0; i--) if (d->lines[i].source_line > 0) return d->lines[i].source_line - 1;
    for (int i = line + 1; i < (int)d->nline; i++) if (d->lines[i].source_line > 0) return d->lines[i].source_line - 1;
    return -1;
}

/* Rendered Doc line for a 0-based source line: the last line tagged at or before
 * it (tags are monotonic). -1 if the Doc carries no tags. The exact inverse map
 * for editor->reader. */
static int src_doc_line(const Doc *d, int srcline) {
    int best = -1, tagged = 0;
    for (int i = 0; i < (int)d->nline; i++) {
        int s = d->lines[i].source_line;
        if (s > 0) { tagged = 1; if (s - 1 <= srcline) best = i; else break; }
    }
    return tagged ? (best < 0 ? 0 : best) : -1;
}

/* Seed the editor from the current source, mapping the Reader cursor onto it. */
static void seed_editor(App *a) {
    editor_init(&a->ed, a->src, a->srclen);
    int sl = doc_src_line(a->doc, a->rcur_line);
    if (sl < 0) sl = map_line(a->rcur_line, (int)a->doc->nline, (int)a->ed.n);
    if (sl >= (int)a->ed.n) sl = (int)a->ed.n - 1;
    if (sl < 0) sl = 0;
    a->ed.cy = sl;
    ELine *L = &a->ed.lines[sl];
    a->ed.cx = (int)ed_col_to_byte(L->b ? L->b : "", L->len, a->rcur_col);
    a->ed.goal_col = -1;
}

/* Enter Insert mode (full-screen editor). */
static void enter_insert(App *a) { seed_editor(a); a->mode = MODE_INSERT; }

/* Enter Split mode (editor + live preview). */
static void enter_split(App *a) { seed_editor(a); a->mode = MODE_SPLIT; render_preview(a); }

/* Copy the editor's buffer back into the app source (editor stays alive). */
static void sync_source(App *a) {
    size_t nlen; char *ns = editor_source(&a->ed, &nlen);
    if (ns) { free(a->src); a->src = ns; a->srclen = nlen; }
    if (a->ed.dirty) a->dirty = 1;
}

/* Leave Insert/Split back to Reader: sync the buffer into the source, re-render,
 * and map the editor cursor back onto the rendered view. */
static void leave_editor(App *a) {
    int src_cy = a->ed.cy, src_n = (int)a->ed.n;
    int rcol = editor_cursor_col(&a->ed);
    sync_source(a);
    editor_free(&a->ed);
    if (a->preview) { doc_free(a->preview); a->preview = NULL; }
    a->mode = MODE_READER;
    rerender(a);
    int rl = src_doc_line(a->doc, src_cy);
    a->rcur_line = (rl >= 0) ? rl : map_line(src_cy, src_n, (int)a->doc->nline);
    a->rcur_col = rcol;
    a->rcur_goal = rcol;
    reader_clamp_cursor(a);
}

/* Draw the help overlay: a centered list of key bindings. */
static void draw_help(App *a) {
    static const char *lines[] = {
        "  glance — key bindings",
        "",
        "  Reader",
        "    h j k l / arrows    move cursor",
        "    g / G               top / bottom",
        "    Ctrl-D / Ctrl-U     half page down / up",
        "    PgDn / PgUp         page down / up",
        "    i                   insert mode (edit)",
        "    e                   split: editor + live preview",
        "    t                   table of contents",
        "    /                   search    (n / N next / prev)",
        "    Ctrl-S              save",
        "    :w :wq :q :q!       write / quit",
        "    ?                   toggle this help",
        "",
        "  Vault (linked notes)",
        "    Enter               follow link / [[wikilink]] under cursor",
        "    -  / Ctrl-O         back to the previous file",
        "    b                   backlinks (files that link here)",
        "    Ctrl-G              graph explorer (links in / out)",
        "",
        "  Insert / Split",
        "    Esc                 back to reader",
        "    Ctrl-S              save",
        "",
        "  press any key to close",
    };
    int n = (int)(sizeof lines / sizeof lines[0]);
    struct ncplane *p = a->plane;
    RGB bg = a->dark ? (RGB){25, 25, 32} : (RGB){240, 240, 245};
    RGB fg = a->dark ? (RGB){220, 220, 220} : (RGB){20, 20, 20};
    int top = (a->rows - n) / 2; if (top < 0) top = 0;
    ncplane_set_styles(p, NCSTYLE_NONE);
    ncplane_set_fg_rgb8(p, fg.r, fg.g, fg.b);
    ncplane_set_bg_rgb8(p, bg.r, bg.g, bg.b);
    for (int i = 0; i < n && top + i < a->rows; i++) {
        for (int x = 0; x < a->cols; x++) ncplane_putchar_yx(p, top + i, x, ' ');
        ncplane_putstr_yx(p, top + i, 0, lines[i]);
    }
    ncplane_set_fg_default(p);
    ncplane_set_bg_default(p);
}

/* Draw whichever mode is active, plus the help overlay when open. */
static void redraw(App *a) {
    clear_images(a);                /* drop last frame's image planes */
    if (a->graphmode)               draw_graph(a);
    else if (a->mode == MODE_READER)     draw_reader(a);
    else if (a->mode == MODE_SPLIT) draw_split(a);
    else                            draw_editor(a);
    if (a->helpmode) { draw_help(a); notcurses_render(a->nc); }
}

/* ---- input ---------------------------------------------------------------- */

/* Insert the key's effective text — the character the user actually meant,
 * with layout/modifier composition applied. notcurses reports the physical key
 * in id/utf8 and the composed result in eff_text (e.g. '#' for Option+à on an
 * Italian layout), so eff_text is the preferred source; fall back to utf8 when
 * it is empty. Ctrl-chords are commands, not text. Returns 1 if it inserted. */
static int try_insert_text(Editor *e, const ncinput *ni) {
    if (ncinput_ctrl_p(ni)) return 0;
    char buf[NCINPUT_MAX_EFF_TEXT_CODEPOINTS * 4];
    int len = 0;
    for (int i = 0; i < NCINPUT_MAX_EFF_TEXT_CODEPOINTS && ni->eff_text[i]; i++) {
        uint32_t cp = ni->eff_text[i];
        if (cp < 0x20 || cp == 0x7f || cp > 0x10FFFF) continue;
        len += u8_encode(cp, buf + len);
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

/* Write the current source to disk atomically, updating the status message and
 * the dirty flag. Returns 0 on success. */
static int save_file(App *a) {
    if (!a->path) { snprintf(a->msg, sizeof a->msg, "no file name — can't write (reading stdin)"); return -1; }
    if (atomic_write(a->path, a->src, a->srclen) != 0) {
        snprintf(a->msg, sizeof a->msg, "write failed: %s", strerror(errno));
        return -1;
    }
    a->dirty = 0;
    snprintf(a->msg, sizeof a->msg, "\"%s\" %zuB written", a->title, a->srclen);
    return 0;
}

/* Execute a typed ':' command. Returns 0 to quit, 1 to keep running. */
static int run_command(App *a) {
    const char *c = a->cmdbuf;
    a->cmdmode = 0;
    int ret = 1;
    if (!strcmp(c, "q") || !strcmp(c, "quit")) {
        if (a->dirty) snprintf(a->msg, sizeof a->msg, "unsaved changes — :w to save or :q! to discard");
        else ret = 0;
    } else if (!strcmp(c, "q!")) {
        ret = 0;
    } else if (!strcmp(c, "w")) {
        save_file(a);
    } else if (!strcmp(c, "wq") || !strcmp(c, "x")) {
        if (save_file(a) == 0) ret = 0;
    } else if (!strcmp(c, "graph")) {
        open_graph(a);
    } else if (c[0]) {
        snprintf(a->msg, sizeof a->msg, "not a command: :%s", c);
    }
    a->cmdbuf[0] = '\0'; a->cmdlen = 0;
    return ret;
}

/* Edit the ':' command line: type, backspace, Enter to run, Esc to cancel. */
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

/* Move the block cursor onto search hit `idx` (wrapping handled by caller). */
static void focus_hit(App *a, int idx) {
    if (a->hits.n == 0) return;
    a->cur_hit = (idx % a->hits.n + a->hits.n) % a->hits.n;
    a->rcur_line = a->hits.v[a->cur_hit].line;
    a->rcur_col = a->hits.v[a->cur_hit].col;
    a->rcur_goal = a->rcur_col;
    reader_clamp_cursor(a);
}

/* Run the typed query, populate hits, and jump to the first one. */
static void run_search(App *a) {
    a->searchmode = 0;                       /* searchbuf stays as the query */
    search_doc(a->doc, a->searchbuf, &a->hits);
    a->cur_hit = -1;
    if (a->hits.n > 0) focus_hit(a, 0);
}

/* Clear the active search and its highlights. */
static void clear_search(App *a) {
    hits_free(&a->hits);
    a->cur_hit = -1;
    a->searchbuf[0] = '\0';
    a->searchlen = 0;
}

/* Edit the '/' search prompt: type, backspace, Enter to run, Esc to cancel. */
static int handle_search(App *a, uint32_t id, const ncinput *ni) {
    if (id == NCKEY_ESC) { a->searchmode = 0; a->searchbuf[0] = '\0'; a->searchlen = 0; return 1; }
    if (id == NCKEY_ENTER || id == '\r' || id == '\n') { run_search(a); return 1; }
    if (id == NCKEY_BACKSPACE || id == 0x08 || id == 0x7f) {
        if (a->searchlen > 0) a->searchbuf[--a->searchlen] = '\0';
        else a->searchmode = 0;
        return 1;
    }
    if (!ncinput_ctrl_p(ni) && ni->eff_text[0] >= 0x20) {
        char buf[8]; int k = u8_encode(ni->eff_text[0], buf);
        if (a->searchlen + k < (int)sizeof(a->searchbuf) - 1) {
            memcpy(a->searchbuf + a->searchlen, buf, k);
            a->searchlen += k; a->searchbuf[a->searchlen] = '\0';
        }
    }
    return 1;
}

/* Open the TOC panel, selecting the heading at or before the cursor line. */
static void open_toc(App *a) {
    toc_free(&a->toc);                       /* rebuild fresh from the current Doc */
    toc_build(a->doc, &a->toc);
    if (a->toc.n == 0) return;
    a->toc_sel = 0;
    for (int i = 0; i < a->toc.n; i++)
        if (a->toc.v[i].line <= a->rcur_line) a->toc_sel = i;
    a->tocmode = 1;
}

/* TOC-panel keys: move the selection, Enter jumps, t/Esc/q close. */
static int handle_toc(App *a, uint32_t id, const ncinput *ni) {
    if (id == 'j' || id == NCKEY_DOWN)      { if (a->toc_sel + 1 < a->toc.n) a->toc_sel++; }
    else if (id == 'k' || id == NCKEY_UP)   { if (a->toc_sel > 0) a->toc_sel--; }
    else if (id == 'g' || id == NCKEY_HOME) a->toc_sel = 0;
    else if (id == 'G' || id == NCKEY_END)  a->toc_sel = a->toc.n - 1;
    else if (id == NCKEY_ENTER || id == '\r' || id == '\n') {
        a->rcur_line = a->toc.v[a->toc_sel].line;
        a->rcur_col = 0; a->rcur_goal = 0;
        reader_clamp_cursor(a);
        a->tocmode = 0;
    } else if (id == 't' || id == NCKEY_ESC || id == 'q' ||
               (id == 'c' && ncinput_ctrl_p(ni))) {
        a->tocmode = 0;
    }
    return 1;
}

/* Yank the selected visual lines (their plain text) to the system clipboard. */
static void yank_selection(App *a) {
    int lo = a->visual_anchor, hi = a->rcur_line;
    if (lo > hi) { int t = lo; lo = hi; hi = t; }
    /* gather the run text of each selected Doc line, joined by newlines */
    size_t cap = 0;
    for (int li = lo; li <= hi; li++) {
        for (size_t j = 0; j < a->doc->lines[li].nrun; j++) cap += a->doc->lines[li].runs[j].len;
        cap += 1;
    }
    char *buf = malloc(cap + 1);
    if (buf) {
        size_t p = 0;
        for (int li = lo; li <= hi; li++) {
            Line *L = &a->doc->lines[li];
            for (size_t j = 0; j < L->nrun; j++) {
                memcpy(buf + p, L->runs[j].text, L->runs[j].len);
                p += L->runs[j].len;
            }
            buf[p++] = '\n';
        }
        int ok = clip_copy(buf, p) == 0;
        snprintf(a->msg, sizeof a->msg, ok ? "%d line%s yanked to clipboard" : "clipboard unavailable",
                 hi - lo + 1, hi - lo ? "s" : "");
        free(buf);
    }
    a->visualmode = 0;
}

/* Visual-line keys: extend the selection, y yanks, Esc/V cancel. */
static int handle_visual(App *a, uint32_t id, const ncinput *ni) {
    int body = content_rows(a);
    if (id == 'j' || id == NCKEY_DOWN)        a->rcur_line++;
    else if (id == 'k' || id == NCKEY_UP)     a->rcur_line--;
    else if (id == 'g' || id == NCKEY_HOME)   a->rcur_line = 0;
    else if (id == 'G' || id == NCKEY_END)    a->rcur_line = (int)a->doc->nline - 1;
    else if (id == NCKEY_PGDOWN)              a->rcur_line += body;
    else if (id == NCKEY_PGUP)                a->rcur_line -= body;
    else if (id == 'y')                       yank_selection(a);
    else if (id == 'v' || id == 'V' || id == NCKEY_ESC ||
             (id == 'c' && ncinput_ctrl_p(ni))) a->visualmode = 0;
    reader_clamp_cursor(a);
    return 1;
}

/* Return the link target of the run under the Reader cursor, or NULL. */
static const char *link_at_cursor(App *a) {
    if (a->rcur_line >= (int)a->doc->nline) return NULL;
    Line *L = &a->doc->lines[a->rcur_line];
    int x = 0;
    for (size_t j = 0; j < L->nrun; j++) {
        int w = u8_width(L->runs[j].text, L->runs[j].len);
        if (a->rcur_col >= x && a->rcur_col < x + w) return L->runs[j].link;
        x += w;
    }
    return NULL;
}

/* ---- cross-file navigation (wikilinks / relative .md links) --------------- */

/* Set the status title to a file's base name, or "stdin". */
static void set_title(App *a, const char *path) {
    if (!path) { snprintf(a->title, sizeof a->title, "stdin"); return; }
    char tmp[1024]; snprintf(tmp, sizeof tmp, "%s", path);
    snprintf(a->title, sizeof a->title, "%s", basename(tmp));
}

/* Resolve `rel` against the directory of `base`; returns an owned path. */
static char *resolve_path(const char *base, const char *rel) {
    if (rel[0] == '/' || !base) return strdup(rel);
    char tmp[1024], out[2048];
    snprintf(tmp, sizeof tmp, "%s", base);
    snprintf(out, sizeof out, "%s/%s", dirname(tmp), rel);
    return strdup(out);
}

/* Point the file watcher at the current file's directory. */
static void rewatch(App *a) {
    watch_close(&a->watch);
    a->wfd = watch_open(&a->watch, a->path);
}

/* Load `path` into the app: swap in its content, re-render, reset the view, and
 * rewatch its directory. Returns 0 on success, -1 if it can't be read. */
static int load_into(App *a, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(a->msg, sizeof a->msg, "can't open %s", path); return -1; }
    size_t nlen; char *ns = read_file(f, &nlen);
    fclose(f);
    if (!ns) return -1;
    free(a->src); a->src = ns; a->srclen = nlen;
    free(a->path); a->path = strdup(path);
    set_title(a, a->path);
    a->dirty = 0;
    clear_search(a);
    a->rcur_line = a->rcur_col = a->rcur_goal = a->top = 0;
    rerender(a);
    rewatch(a);
    return 0;
}

/* An external URL we open with the system handler rather than inside glance. */
static int is_external(const char *u) {
    return strncmp(u, "http://", 7) == 0 || strncmp(u, "https://", 8) == 0 ||
           strncmp(u, "mailto:", 7) == 0 || strncmp(u, "ftp://", 6) == 0;
}

/* Load `full` into the app, pushing the current file onto the back-stack. */
static void navigate_to(App *a, const char *full) {
    char *cur = a->path ? strdup(a->path) : NULL;
    if (load_into(a, full) == 0) {
        if (cur && a->nback < 64) a->back[a->nback++] = cur; else free(cur);
        snprintf(a->msg, sizeof a->msg, "→ %s", a->title);
    } else {
        free(cur);
    }
}

/* Follow a link: external URLs open in the browser; a relative .md path resolves
 * against the current file; a bare [[wikilink]] name is searched across the
 * whole vault (recursively) — the way Obsidian resolves links. */
static void open_link_target(App *a, const char *u) {
    if (is_external(u)) { open_url(u); snprintf(a->msg, sizeof a->msg, "opening %s", u); return; }
    if (!a->path) { snprintf(a->msg, sizeof a->msg, "can't follow links from stdin"); return; }
    if (a->dirty) { snprintf(a->msg, sizeof a->msg, "unsaved changes — :w before following links"); return; }

    size_t ul = strlen(u);
    int is_md = ul > 3 && strcasecmp(u + ul - 3, ".md") == 0;
    if (strchr(u, '/') || is_md) {                 /* a path: resolve relatively */
        char file[1024];
        snprintf(file, sizeof file, is_md ? "%s" : "%s.md", u);
        char *full = resolve_path(a->path, file);
        navigate_to(a, full);
        free(full);
    } else {                                        /* a wikilink name: vault-wide */
        char root[4096]; vault_root(a->path, root, sizeof root);
        char *full = vault_find(root, u);
        if (!full) { snprintf(a->msg, sizeof a->msg, "not found in vault: [[%s]]", u); return; }
        navigate_to(a, full);
        free(full);
    }
}

/* Return to the previously viewed file. */
static void go_back(App *a) {
    if (a->nback == 0) { snprintf(a->msg, sizeof a->msg, "no file to go back to"); return; }
    if (a->dirty) { snprintf(a->msg, sizeof a->msg, "unsaved changes — :w first"); return; }
    char *prev = a->back[--a->nback];
    if (load_into(a, prev) == 0) snprintf(a->msg, sizeof a->msg, "← %s", a->title);
    free(prev);
}

/* Scan the whole vault (recursively) for .md files that link to the current
 * one, and open the backlinks panel listing them. */
static void open_backlinks(App *a) {
    for (int i = 0; i < a->nbl; i++) free(a->bl[i]);
    a->nbl = a->bl_sel = 0;
    if (!a->path) { snprintf(a->msg, sizeof a->msg, "no vault (reading stdin)"); return; }

    char root[4096]; vault_root(a->path, root, sizeof root);
    char self[256]; vault_stem(a->path, self, sizeof self);
    char selfreal[4096]; if (!realpath(a->path, selfreal)) selfreal[0] = '\0';

    VFiles files = {0};
    vault_scan(root, &files);
    for (int i = 0; i < files.n && a->nbl < 256; i++) {
        char full[8192];
        snprintf(full, sizeof full, "%s/%s", root, files.v[i]);
        char freal[4096];
        if (realpath(full, freal) && selfreal[0] && strcmp(freal, selfreal) == 0)
            continue;                                        /* skip the file itself */
        FILE *f = fopen(full, "rb"); if (!f) continue;
        size_t len; char *s = read_file(f, &len); fclose(f); if (!s) continue;
        VLinks vl = {0}; vault_links(s, len, &vl);
        for (int k = 0; k < vl.n; k++) {
            char t[256]; vault_stem(vl.v[k].target, t, sizeof t);
            if (strcasecmp(t, self) == 0) { a->bl[a->nbl++] = strdup(full); break; }
        }
        vlinks_free(&vl);
        free(s);
    }
    vfiles_free(&files);
    if (a->nbl == 0) { snprintf(a->msg, sizeof a->msg, "no backlinks to %s", a->title); return; }
    a->blmode = 1;
}

/* Build the vault graph and open the explorer centred on the current file. */
static void open_graph(App *a) {
    if (!a->path) { snprintf(a->msg, sizeof a->msg, "no vault (reading stdin)"); return; }
    vault_root(a->path, a->graph_root, sizeof a->graph_root);
    graph_free(&a->graph);
    graph_build(a->graph_root, &a->graph);
    a->graph_focus = graph_find(&a->graph, a->path);
    if (a->graph_focus < 0) { snprintf(a->msg, sizeof a->msg, "current file is not in the vault"); return; }
    a->graph_sel = 0;
    a->graphmode = 1;
}

/* Graph-explorer keys: j/k select a neighbour, Enter opens it, Space re-centres
 * the graph on it, Esc/q/Ctrl-G close. */
static int handle_graph(App *a, uint32_t id, const ncinput *ni) {
    int back[256], out[256], nb, no;
    graph_neighbors(a, back, &nb, out, &no);
    int total = nb + no;

    if (id == 'j' || id == NCKEY_DOWN)      { if (a->graph_sel + 1 < total) a->graph_sel++; }
    else if (id == 'k' || id == NCKEY_UP)   { if (a->graph_sel > 0) a->graph_sel--; }
    else if (id == 'h' || id == NCKEY_LEFT) {       /* outbound column -> backlinks */
        if (a->graph_sel >= nb && nb > 0) { int row = a->graph_sel - nb; a->graph_sel = row < nb ? row : nb - 1; }
    }
    else if (id == 'l' || id == NCKEY_RIGHT) {      /* backlinks column -> outbound */
        if (a->graph_sel < nb && no > 0) { a->graph_sel = nb + (a->graph_sel < no ? a->graph_sel : no - 1); }
    }
    else if (total && (id == NCKEY_ENTER || id == '\r' || id == '\n')) {
        int node = a->graph_sel < nb ? back[a->graph_sel] : out[a->graph_sel - nb];
        char full[8192];
        snprintf(full, sizeof full, "%s/%s", a->graph_root, a->graph.node[node]);
        a->graphmode = 0;
        if (!a->dirty) navigate_to(a, full);
        else snprintf(a->msg, sizeof a->msg, "unsaved changes — :w first");
    } else if (total && id == ' ') {                 /* re-centre on the selection */
        a->graph_focus = a->graph_sel < nb ? back[a->graph_sel] : out[a->graph_sel - nb];
        a->graph_sel = 0;
    } else if (id == NCKEY_ESC || id == 'q' || id == 0x07 ||
               (id == 'g' && ncinput_ctrl_p(ni)) || (id == 'c' && ncinput_ctrl_p(ni))) {
        a->graphmode = 0;
    }
    return 1;
}

/* Backlinks-panel keys: move the selection, Enter opens, b/Esc/q close. */
static int handle_backlinks(App *a, uint32_t id, const ncinput *ni) {
    if (id == 'j' || id == NCKEY_DOWN)      { if (a->bl_sel + 1 < a->nbl) a->bl_sel++; }
    else if (id == 'k' || id == NCKEY_UP)   { if (a->bl_sel > 0) a->bl_sel--; }
    else if (id == NCKEY_ENTER || id == '\r' || id == '\n') {
        a->blmode = 0;
        if (a->nbl && !a->dirty) navigate_to(a, a->bl[a->bl_sel]);
        else if (a->dirty) snprintf(a->msg, sizeof a->msg, "unsaved changes — :w first");
    } else if (id == 'b' || id == NCKEY_ESC || id == 'q' ||
               (id == 'c' && ncinput_ctrl_p(ni))) {
        a->blmode = 0;
    }
    return 1;
}

/* Reader-mode keys: move the block cursor, search, enter Insert/command, quit. */
static int handle_reader(App *a, uint32_t id, const ncinput *ni) {
    int body = content_rows(a);
    if (id == 'c' && ncinput_ctrl_p(ni))         return 0;   /* escape hatch */
    else if (id == ':')                          { a->cmdmode = 1; a->cmdbuf[0] = '\0'; a->cmdlen = 0; }
    else if (id == '/')                          { a->searchmode = 1; a->searchbuf[0] = '\0'; a->searchlen = 0; }
    else if (id == 't')                          open_toc(a);
    else if (id == 'b')                          open_backlinks(a);
    else if ((id == 'g' && ncinput_ctrl_p(ni)) || id == 0x07) open_graph(a);  /* graph (Ctrl-G or :graph) */
    else if (id == '?')                          a->helpmode = 1;
    else if (id == 's' && ncinput_ctrl_p(ni))    save_file(a);
    else if (id == 'n' && a->hits.n)             focus_hit(a, a->cur_hit + 1);
    else if (id == 'N' && a->hits.n)             focus_hit(a, a->cur_hit - 1);
    else if (id == NCKEY_ESC)                    clear_search(a);
    else if (id == 'i')                          enter_insert(a);  /* vi-style */
    else if (id == 'e')                          enter_split(a);   /* editor + preview */
    else if (id == 'v' || id == 'V')             { a->visualmode = 1; a->visual_anchor = a->rcur_line; }
    else if (id == NCKEY_ENTER || id == '\r' || id == '\n') {
        const char *u = link_at_cursor(a);
        if (u) open_link_target(a, u);
        else snprintf(a->msg, sizeof a->msg, "no link under cursor");
    }
    else if (id == '-' || (id == 'o' && ncinput_ctrl_p(ni)))  go_back(a);
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

/* Insert text with bracket auto-pairing: an opener adds its closer (cursor in
 * between), and typing a closer that already sits at the cursor steps over it. */
static void insert_with_pairing(Editor *e, uint32_t id, const ncinput *ni) {
    uint32_t cp = ni->eff_text[0] ? ni->eff_text[0] : id;
    ELine *L = &e->lines[e->cy];
    char next = (size_t)e->cx < L->len ? L->b[e->cx] : 0;
    if (pair_should_skip(cp, next)) { editor_right(e); return; }
    if (!try_insert_text(e, ni)) return;
    char close = pair_closer(cp);
    if (close) { editor_insert(e, &close, 1); editor_left(e); }
}

/* Apply one editing key (motion, structural edit, or text entry) to e. */
static void apply_edit_key(Editor *e, uint32_t id, const ncinput *ni) {
    if (id == NCKEY_ENTER || id == '\r' || id == '\n') editor_newline(e);
    else if (id == NCKEY_BACKSPACE || id == 0x08 || id == 0x7f) editor_backspace(e);
    else if (id == NCKEY_DEL)   editor_delete(e);
    else if (id == NCKEY_LEFT)  editor_left(e);
    else if (id == NCKEY_RIGHT) editor_right(e);
    else if (id == NCKEY_UP)    editor_up(e);
    else if (id == NCKEY_DOWN)  editor_down(e);
    else if (id == NCKEY_HOME)  editor_home(e);
    else if (id == NCKEY_END)   editor_end(e);
    else if (id == NCKEY_TAB || id == '\t') editor_insert(e, "    ", 4);
    else insert_with_pairing(e, id, ni);
}

/* Paste an image from the system clipboard: save it next to the document as a
 * PNG and insert a Markdown reference at the cursor. Bound to Ctrl-V because the
 * terminal consumes Cmd-V and only ever delivers clipboard text, never image
 * bytes. Needs a saved file, so the image has a folder to live in. */
static void paste_image(App *a) {
    if (!a->path) { snprintf(a->msg, sizeof a->msg, "save the file first to paste an image"); return; }

    char dirbuf[4096];
    const char *d = doc_dir(a, dirbuf, sizeof dirbuf);
    char dir[4096]; snprintf(dir, sizeof dir, "%s", d ? d : ".");

    char basebuf[4096]; snprintf(basebuf, sizeof basebuf, "%s", a->path);
    char stem[256]; snprintf(stem, sizeof stem, "%s", basename(basebuf));
    char *dot = strrchr(stem, '.'); if (dot && dot != stem) *dot = '\0';
    for (char *p = stem; *p; p++) if (*p == ' ') *p = '-';   /* keep the link space-free */

    char fname[300], full[4500];
    for (int n = 1; ; n++) {                       /* first free <stem>-N.png */
        snprintf(fname, sizeof fname, "%s-%d.png", stem, n);
        snprintf(full, sizeof full, "%s/%s", dir, fname);
        if (access(full, F_OK) != 0) break;
        if (n >= 9999) { snprintf(a->msg, sizeof a->msg, "too many pasted images"); return; }
    }

    if (clip_image_save(full)) {
        char ref[400];
        int rn = snprintf(ref, sizeof ref, "![](%s)", fname);
        editor_insert(&a->ed, ref, (size_t)rn);
        a->ed.dirty = a->dirty = 1;
        snprintf(a->msg, sizeof a->msg, "pasted image -> %s", fname);
    } else {
        snprintf(a->msg, sizeof a->msg, "no image in clipboard");
    }
}

/* Insert-mode keys: Esc leaves, Ctrl-S saves, Ctrl-V pastes an image, else edits. */
static int handle_insert(App *a, uint32_t id, const ncinput *ni) {
    if (id == NCKEY_ESC) leave_editor(a);
    else if (id == 's' && ncinput_ctrl_p(ni)) { sync_source(a); save_file(a); }
    else if (id == 'v' && ncinput_ctrl_p(ni)) paste_image(a);
    else apply_edit_key(&a->ed, id, ni);
    return 1;
}

/* Split-mode keys: same editing as Insert, plus a live preview refresh. */
static int handle_split(App *a, uint32_t id, const ncinput *ni) {
    if (id == NCKEY_ESC) leave_editor(a);
    else if (id == 's' && ncinput_ctrl_p(ni)) { sync_source(a); save_file(a); }
    else if (id == 'v' && ncinput_ctrl_p(ni)) { paste_image(a); render_preview(a); }
    else { apply_edit_key(&a->ed, id, ni); render_preview(a); }
    return 1;
}

/* Diagnostic loop printing each key's raw fields; see tui.h. */
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

/* Handle one input event; returns 0 to quit, 1 to keep running. */
static int dispatch_key(App *a, uint32_t id, ncinput *ni) {
    if (ni->evtype == NCTYPE_RELEASE) return 1;
    a->msg[0] = '\0';                            /* clear the transient message */
    if (a->helpmode) { a->helpmode = 0; return 1; }   /* any key closes help */
    if (id == NCKEY_RESIZE) {
        if (a->mode == MODE_READER) rerender(a);
        else update_dims(a);
        return 1;
    }
    if (a->cmdmode)    return handle_command(a, id, ni);
    if (a->searchmode) return handle_search(a, id, ni);
    if (a->tocmode)    return handle_toc(a, id, ni);
    if (a->blmode)     return handle_backlinks(a, id, ni);
    if (a->graphmode)  return handle_graph(a, id, ni);
    if (a->visualmode) return handle_visual(a, id, ni);
    if (a->mode == MODE_READER) return handle_reader(a, id, ni);
    if (a->mode == MODE_SPLIT)  return handle_split(a, id, ni);
    return handle_insert(a, id, ni);
}

/* Re-read the file after an external change. Reloads only in Reader mode with
 * no unsaved edits; identical content (e.g. our own save) is ignored silently. */
static void reload_file(App *a) {
    if (!a->path) return;
    FILE *f = fopen(a->path, "rb");
    if (!f) return;
    size_t nlen; char *ns = read_file(f, &nlen);
    fclose(f);
    if (!ns) return;
    if (nlen == a->srclen && memcmp(ns, a->src, nlen) == 0) { free(ns); return; }
    if (a->dirty || a->mode != MODE_READER) {
        snprintf(a->msg, sizeof a->msg, "file changed on disk — unsaved edits kept");
        free(ns);
        return;
    }
    free(a->src); a->src = ns; a->srclen = nlen;
    rerender(a);
    reader_clamp_cursor(a);
    snprintf(a->msg, sizeof a->msg, "reloaded from disk");
}

/* Bring up notcurses, then loop: wait for terminal input or a file change,
 * dispatch, redraw. See tui.h. Owns a mutable copy of the source. */
int tui_run(const char *src, unsigned long len, const char *path, const char *title) {
    term_kbd_reset();   /* normalize before notcurses probes (slice-2 fix) */

    notcurses_options opts;
    memset(&opts, 0, sizeof opts);
    opts.flags = NCOPTION_SUPPRESS_BANNERS;   /* let notcurses own signal cleanup */

    App a;
    memset(&a, 0, sizeof a);
    a.mode = MODE_READER;
    a.path = path ? strdup(path) : NULL;
    snprintf(a.title, sizeof a.title, "%s", title ? title : "stdin");
    a.src = malloc(len + 1);
    if (!a.src) { free(a.path); return 1; }
    memcpy(a.src, src, len); a.src[len] = '\0'; a.srclen = len;

    a.nc = notcurses_init(&opts, NULL);
    if (!a.nc) { free(a.src); return 1; }
    a.plane = notcurses_stdplane(a.nc);
    a.dark = detect_dark(a.nc);   /* theme follows the terminal background */

    g_nc = a.nc;
    term_kbd_reset();              /* run in legacy keyboard mode (see above) */
    atexit(term_kbd_reset);        /* and restore on any normal exit path */

    if (rerender(&a) != 0) { shutdown_tui(); free(a.src); return 1; }
    redraw(&a);

    /* Watch the file for external edits, and poll it alongside terminal input.
     * The watcher fd lives in the App so cross-file navigation can re-point it. */
    a.watch.kq = a.watch.dir = -1;
    a.wfd = watch_open(&a.watch, a.path);
    int infd = notcurses_inputready_fd(a.nc);

    int running = 1;
    ncinput ni; uint32_t id;
    while (running) {
        if (infd < 0) {                          /* no pollable fd: just block */
            id = notcurses_get_blocking(a.nc, &ni);
            if (id == (uint32_t)-1) break;
            running = dispatch_key(&a, id, &ni);
            if (running) redraw(&a);
            continue;
        }
        struct pollfd fds[2];
        fds[0].fd = infd; fds[0].events = POLLIN; fds[0].revents = 0;
        int nfds = 1;
        if (a.wfd >= 0) { fds[1].fd = a.wfd; fds[1].events = POLLIN; fds[1].revents = 0; nfds = 2; }
        if (poll(fds, nfds, -1) < 0) { if (errno == EINTR) continue; break; }
        if (nfds == 2 && (fds[1].revents & POLLIN)) {
            watch_drain(&a.watch); reload_file(&a); redraw(&a);
        }
        if (!(fds[0].revents & POLLIN)) continue;
        /* drain all available key events, then redraw once */
        while ((id = notcurses_get_nblock(a.nc, &ni)) != 0 && id != (uint32_t)-1) {
            running = dispatch_key(&a, id, &ni);
            if (!running) break;
        }
        if (running) redraw(&a);
    }
    watch_close(&a.watch);

    /* drain anything the terminal already queued so leftover bytes don't spill
     * onto the shell prompt after we exit */
    ncinput drain; uint32_t d;
    while ((d = notcurses_get_nblock(a.nc, &drain)) != 0 && d != (uint32_t)-1) { }

    if (a.mode == MODE_INSERT || a.mode == MODE_SPLIT) editor_free(&a.ed);
    if (a.preview) doc_free(a.preview);
    hits_free(&a.hits);
    toc_free(&a.toc);
    doc_free(a.doc);
    shutdown_tui();
    for (int i = 0; i < a.nbl; i++) free(a.bl[i]);
    for (int i = 0; i < a.nback; i++) free(a.back[i]);
    graph_free(&a.graph);
    free(a.path);
    free(a.src);
    return 0;
}
