/* render.c — glance's Markdown renderer: md4c → structured Doc.
 *
 * md4c parses; the callbacks here build a Doc (lines of styled runs). Inline
 * flow is word-wrapped to the target width. Block constructs (headings, lists,
 * quotes, code fences, tables, rules) get their own styling. A Style stack
 * tracks nested inline emphasis so leaving a span restores exactly the style
 * that was active before it (including a heading's colour).
 */
#include "render.h"
#include "theme.h"
#include "preprocess.h"
#include "highlight.h"
#include "image_size.h"
#include "util.h"   /* u8_width, path_resolve */

#include <md4c.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- palette -------------------------------------------------------------- */
/* Element colours now come from the active Theme (see theme.h); these thin
 * accessors keep the call sites in this file readable. */

static RGB rgb(uint8_t r, uint8_t g, uint8_t b) { RGB c = {r, g, b}; return c; }

/* Heading colour by level (1..6). */
static RGB heading_fg(const Theme *t, int level) {
    if (level < 1) level = 1;
    if (level > 6) level = 6;
    return t->heading[level - 1];
}
static RGB code_bg(const Theme *t)   { return t->code_bg; }
static RGB code_fg(const Theme *t)   { return t->code_fg; }
static RGB link_fg(const Theme *t)   { return t->link; }
static RGB accent_fg(const Theme *t) { return t->accent; }
static RGB quote_fg(const Theme *t)  { return t->quote; }
static RGB rule_fg(const Theme *t)   { return t->rule; }

/* Foreground for a syntax-highlight token kind. HL_TEXT maps to the plain code
 * colour (set up in theme.c) so un-tokenised code keeps the box's look. */
static RGB hl_fg(HLKind k, const Theme *t) { return t->syntax[k]; }

/* ---- renderer state ------------------------------------------------------- */

#define MAX_LIST  16
#define MAX_STYLE 32
#define MAX_COLS  32

/* A table cell: a small list of styled runs plus its display width. */
typedef struct { Run *runs; size_t nrun, cap; int width; } Cell;
/* A table row: up to MAX_COLS cells; header rows render bold. */
typedef struct { Cell cells[MAX_COLS]; int ncell, header; } TRow;

typedef struct {
    Doc  *doc;
    int   width;
    const Theme *theme;
    const char *basedir;   /* document directory, for sizing local images */

    /* pending visual line being assembled */
    Run  *line; size_t line_n, line_cap;
    int   line_cols, line_has;

    /* pending word (may hold >1 run if styles change mid-word) */
    Run  *word; size_t word_n, word_cap;
    int   word_cols;

    /* current inline style + a save stack for nested spans/blocks */
    Style cur;
    Style stack[MAX_STYLE]; int sp;
    char *cur_link;        /* href of the enclosing link span, or NULL */

    /* block context */
    int     in_code;
    char   *code_buf; size_t code_len, code_cap;
    int     code_lang;     /* hl_lang() id for the current fence, or -1 */
    HLState hl;            /* multi-line highlighter state within the fence */

    /* image span being collected: src + the alt text accumulated from it */
    int    in_image;
    char  *img_src;
    char  *img_alt; size_t img_alt_len, img_alt_cap;

    /* table buffering: rows collected here, emitted aligned on leave TABLE */
    int      in_table, in_cell, tbl_header;
    int      tbl_ncol, tbl_col, tbl_nrow, tbl_rowcap;
    MD_ALIGN tbl_align[MAX_COLS];
    TRow    *tbl_rows;

    int    quote_depth;
    int    list_depth;
    char   list_mark[MAX_LIST];
    int    ol_next[MAX_LIST];
    int    li_pending;
    int    heading;        /* current heading level (inside MD_BLOCK_H) */
    int    heading_start;  /* doc line index where the heading's text begins */
} R;

/* ---- run / line builders -------------------------------------------------- */

/* Append a copy of (text,len,st) to a growable run array. Returns -1 on OOM. */
static int runs_push(Run **arr, size_t *n, size_t *cap,
                     const char *text, size_t len, Style st) {
    if (*n == *cap) {
        size_t nc = *cap ? *cap * 2 : 8;
        Run *p = realloc(*arr, nc * sizeof(Run));
        if (!p) return -1;
        *arr = p; *cap = nc;
    }
    char *t = malloc(len + 1);
    if (!t) return -1;
    memcpy(t, text, len); t[len] = '\0';
    (*arr)[*n].text = t;
    (*arr)[*n].len  = len;
    (*arr)[*n].st   = st;
    (*arr)[*n].link = NULL;
    (*n)++;
    return 0;
}

/* Save / restore the current inline style around a nested span or block. */
static void style_push(R *r) { if (r->sp < MAX_STYLE) r->stack[r->sp++] = r->cur; }
static void style_pop(R *r)  { if (r->sp > 0) r->cur = r->stack[--r->sp]; }

/* commit the pending line (even if empty) as a Doc line, then reset it */
static void line_commit(R *r) {
    Doc *d = r->doc;
    if (d->nline == d->cap) {
        size_t nc = d->cap ? d->cap * 2 : 64;
        Line *p = realloc(d->lines, nc * sizeof(Line));
        if (!p) return;
        d->lines = p; d->cap = nc;
    }
    Line *L = &d->lines[d->nline++];
    L->runs = r->line; L->nrun = r->line_n; L->cap = r->line_cap;
    L->cols = r->line_cols;
    L->source_line = 0;
    L->heading = 0;
    L->fill = 0; L->fill_bg = rgb(0, 0, 0);
    L->image = NULL; L->img_rows = 0;
    /* hand ownership of the run array to the line; start a fresh one */
    r->line = NULL; r->line_n = 0; r->line_cap = 0;
    r->line_cols = 0; r->line_has = 0;
}

/* ensure the current line has its block indentation + quote bar prepended */
static void line_start_indent(R *r) {
    Style plain; memset(&plain, 0, sizeof plain);
    int pad = r->list_depth * 2 + r->quote_depth * 2;
    if (pad > 0) {
        char sp[64];
        if (pad > (int)sizeof sp - 1) pad = sizeof sp - 1;
        memset(sp, ' ', pad);
        runs_push(&r->line, &r->line_n, &r->line_cap, sp, pad, plain);
        r->line_cols += pad;
    }
    if (r->quote_depth > 0) {
        Style q; memset(&q, 0, sizeof q);
        q.has_fg = 1; q.fg = quote_fg(r->theme); q.dim = 1;
        runs_push(&r->line, &r->line_n, &r->line_cap, "\xe2\x94\x82 ", 4, q); /* "│ " */
        r->line_cols += 2;
    }
}

/* flush the pending word onto the current line, wrapping if it would overflow */
static void flush_word(R *r) {
    if (r->word_n == 0) return;
    int need = r->word_cols + (r->line_has ? 1 : 0);
    if (r->line_has && r->line_cols + need > r->width) {
        line_commit(r);
    }
    if (!r->line_has) {
        line_start_indent(r);
    } else {
        Style plain; memset(&plain, 0, sizeof plain);
        runs_push(&r->line, &r->line_n, &r->line_cap, " ", 1, plain);
        r->line_cols += 1;
    }
    for (size_t i = 0; i < r->word_n; i++) {
        Run *w = &r->word[i];
        runs_push(&r->line, &r->line_n, &r->line_cap, w->text, w->len, w->st);
        r->line[r->line_n - 1].link = w->link;   /* transfer link ownership */
        free(w->text);
    }
    r->line_cols += r->word_cols;
    r->line_has = 1;
    r->word_n = 0;
    r->word_cols = 0;
}

/* push a styled run onto the pending word, tagging it with the active link */
static void word_run(R *r, const char *text, size_t len, Style st) {
    runs_push(&r->word, &r->word_n, &r->word_cap, text, len, st);
    if (r->cur_link) r->word[r->word_n - 1].link = strdup(r->cur_link);
    r->word_cols += u8_width(text, len);
}

/* feed normal inline text, splitting on whitespace into wrappable words */
static void feed_text(R *r, const char *s, size_t n) {
    size_t i = 0;
    while (i < n) {
        if (s[i] == ' ' || s[i] == '\t') {
            flush_word(r);
            while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
            continue;
        }
        size_t start = i;
        while (i < n && s[i] != ' ' && s[i] != '\t') i++;
        word_run(r, s + start, i - start, r->cur);
    }
}

/* Accumulate raw code-block bytes; split into lines on leave_block. */
static void code_buf_append(R *r, const char *s, size_t n) {
    if (r->code_len + n + 1 > r->code_cap) {
        size_t nc = r->code_cap ? r->code_cap : 256;
        while (r->code_len + n + 1 > nc) nc *= 2;
        char *p = realloc(r->code_buf, nc);
        if (!p) return;
        r->code_buf = p; r->code_cap = nc;
    }
    memcpy(r->code_buf + r->code_len, s, n);
    r->code_len += n;
}

/* Push one highlighted span of a code line: token colour over the code bg. */
static void hl_emit_run(void *ud, HLKind kind, const char *s, size_t len) {
    R *r = ud;
    if (len == 0) return;
    Style cs; memset(&cs, 0, sizeof cs);
    cs.has_fg = 1; cs.fg = hl_fg(kind, r->theme);
    cs.has_bg = 1; cs.bg = code_bg(r->theme);
    if (kind == HL_COMMENT) cs.dim = 1;
    runs_push(&r->line, &r->line_n, &r->line_cap, s, len, cs);
    r->line_cols += u8_width(s, len);
}

/* emit one code-block line: indent + (highlighted) text + bg fill to width */
static void code_line(R *r, const char *s, size_t n) {
    line_start_indent(r);
    Style cs; memset(&cs, 0, sizeof cs);
    cs.has_fg = 1; cs.fg = code_fg(r->theme);
    cs.has_bg = 1; cs.bg = code_bg(r->theme);
    /* leading space inside the box */
    runs_push(&r->line, &r->line_n, &r->line_cap, " ", 1, cs);
    r->line_cols += 1;
    if (r->code_lang >= 0) {
        hl_line(r->code_lang, &r->hl, s, n, hl_emit_run, r);
    } else {
        runs_push(&r->line, &r->line_n, &r->line_cap, s, n, cs);
        r->line_cols += u8_width(s, n);
    }
    /* mark the line for bg fill to the right edge */
    r->line_has = 1;
    line_commit(r);
    Line *L = &r->doc->lines[r->doc->nline - 1];
    L->fill = 1; L->fill_bg = code_bg(r->theme);
}

/* ---- tables --------------------------------------------------------------- */
/* A table is buffered whole (its cells stream in as inline text), then emitted
 * as aligned, bordered Doc lines once md4c closes it — only then are all the
 * column widths known. Box-drawing pieces: */
#define BOX_H  "\xe2\x94\x80"  /* ─ */
#define BOX_V  "\xe2\x94\x82"  /* │ */
#define BOX_TL "\xe2\x94\x8c"  /* ┌ */
#define BOX_TM "\xe2\x94\xac"  /* ┬ */
#define BOX_TR "\xe2\x94\x90"  /* ┐ */
#define BOX_ML "\xe2\x94\x9c"  /* ├ */
#define BOX_MM "\xe2\x94\xbc"  /* ┼ */
#define BOX_MR "\xe2\x94\xa4"  /* ┤ */
#define BOX_BL "\xe2\x94\x94"  /* └ */
#define BOX_BM "\xe2\x94\xb4"  /* ┴ */
#define BOX_BR "\xe2\x94\x98"  /* ┘ */

/* Style for the table's box-drawing rules. */
static Style border_style(const Theme *t) {
    Style bs; memset(&bs, 0, sizeof bs);
    bs.has_fg = 1; bs.fg = rule_fg(t); bs.dim = 1;
    return bs;
}

/* Push `k` spaces (plain style) onto the current line. */
static void push_spaces(R *r, int k) {
    if (k <= 0) return;
    char buf[256]; if (k > 255) k = 255;
    memset(buf, ' ', k);
    Style plain; memset(&plain, 0, sizeof plain);
    runs_push(&r->line, &r->line_n, &r->line_cap, buf, k, plain);
    r->line_cols += k;
}

/* The row currently being filled (grows the row array as needed), or NULL. */
static TRow *tbl_currow(R *r) {
    if (r->tbl_nrow >= r->tbl_rowcap) {
        int nc = r->tbl_rowcap ? r->tbl_rowcap * 2 : 8;
        TRow *p = realloc(r->tbl_rows, (size_t)nc * sizeof(TRow));
        if (!p) return NULL;
        r->tbl_rows = p; r->tbl_rowcap = nc;
    }
    return &r->tbl_rows[r->tbl_nrow];
}

/* Append a styled run to the current table cell, carrying any active link. */
static void cell_push(R *r, const char *s, size_t n, Style st) {
    if (r->tbl_nrow >= r->tbl_rowcap || r->tbl_col < 0 || r->tbl_col >= MAX_COLS) return;
    Cell *cell = &r->tbl_rows[r->tbl_nrow].cells[r->tbl_col];
    if (runs_push(&cell->runs, &cell->nrun, &cell->cap, s, n, st) == 0) {
        if (r->cur_link) cell->runs[cell->nrun - 1].link = strdup(r->cur_link);
        cell->width += u8_width(s, n);
    }
}

/* Emit one horizontal rule: left corner, dashes per column, right corner. */
static void emit_border(R *r, const char *L, const char *M, const char *Rt,
                        const int *colw, int ncol) {
    line_start_indent(r);
    int vis = 1; for (int c = 0; c < ncol; c++) vis += colw[c] + 3;  /* 2 pad + 1 junction */
    char *buf = malloc((size_t)vis * 3 + 8);
    if (!buf) return;
    size_t p = 0;
    memcpy(buf + p, L, 3); p += 3;
    for (int c = 0; c < ncol; c++) {
        for (int k = 0; k < colw[c] + 2; k++) { memcpy(buf + p, BOX_H, 3); p += 3; }
        const char *j = (c == ncol - 1) ? Rt : M;
        memcpy(buf + p, j, 3); p += 3;
    }
    runs_push(&r->line, &r->line_n, &r->line_cap, buf, p, border_style(r->theme));
    r->line_cols += vis;
    free(buf);
    r->line_has = 1;
    line_commit(r);
}

/* Emit one content row: vertical rules around each cell, padded per alignment. */
static void emit_row(R *r, TRow *row, const int *colw, int ncol) {
    line_start_indent(r);
    Style bs = border_style(r->theme);
    runs_push(&r->line, &r->line_n, &r->line_cap, BOX_V, 3, bs);
    r->line_cols += 1;
    for (int c = 0; c < ncol; c++) {
        Cell *cell = (c < row->ncell) ? &row->cells[c] : NULL;
        int pad = colw[c] - (cell ? cell->width : 0); if (pad < 0) pad = 0;
        int lpad = 0, rpad = pad;
        if (r->tbl_align[c] == MD_ALIGN_RIGHT)       { lpad = pad; rpad = 0; }
        else if (r->tbl_align[c] == MD_ALIGN_CENTER) { lpad = pad / 2; rpad = pad - lpad; }
        push_spaces(r, 1);          /* left cell padding inside the rule */
        push_spaces(r, lpad);
        if (cell) for (size_t k = 0; k < cell->nrun; k++) {
            Run *rn = &cell->runs[k];
            runs_push(&r->line, &r->line_n, &r->line_cap, rn->text, rn->len, rn->st);
            if (rn->link) r->line[r->line_n - 1].link = strdup(rn->link);
            r->line_cols += u8_width(rn->text, rn->len);
        }
        push_spaces(r, rpad);
        push_spaces(r, 1);
        runs_push(&r->line, &r->line_n, &r->line_cap, BOX_V, 3, bs);
        r->line_cols += 1;
    }
    r->line_has = 1;
    line_commit(r);
}

/* Turn the buffered table into bordered, column-aligned Doc lines. */
static void table_emit(R *r) {
    int ncol = r->tbl_ncol;
    if (ncol <= 0 || ncol > MAX_COLS) return;
    int colw[MAX_COLS];
    for (int c = 0; c < ncol; c++) colw[c] = 1;   /* keep empty columns visible */
    for (int i = 0; i < r->tbl_nrow; i++) {
        TRow *row = &r->tbl_rows[i];
        for (int c = 0; c < ncol && c < row->ncell; c++)
            if (row->cells[c].width > colw[c]) colw[c] = row->cells[c].width;
    }
    emit_border(r, BOX_TL, BOX_TM, BOX_TR, colw, ncol);
    for (int i = 0; i < r->tbl_nrow; i++) {
        TRow *row = &r->tbl_rows[i];
        emit_row(r, row, colw, ncol);
        if (row->header && (i + 1 >= r->tbl_nrow || !r->tbl_rows[i + 1].header))
            emit_border(r, BOX_ML, BOX_MM, BOX_MR, colw, ncol);
    }
    emit_border(r, BOX_BL, BOX_BM, BOX_BR, colw, ncol);
}

/* Free every buffered cell/row and reset table state. */
static void table_free(R *r) {
    for (int i = 0; i < r->tbl_nrow; i++) {
        TRow *row = &r->tbl_rows[i];
        for (int c = 0; c < row->ncell; c++) {
            Cell *cell = &row->cells[c];
            for (size_t k = 0; k < cell->nrun; k++) { free(cell->runs[k].text); free(cell->runs[k].link); }
            free(cell->runs);
        }
    }
    free(r->tbl_rows);
    r->tbl_rows = NULL;
    r->tbl_nrow = r->tbl_rowcap = r->tbl_ncol = r->tbl_col = 0;
    r->in_table = r->in_cell = r->tbl_header = 0;
}

/* ---- images --------------------------------------------------------------- */
/* An image renders as its own block: a placeholder line plus reserved rows the
 * TUI blits the decoded picture over (the CLI just shows the placeholder). */
#define IMG_ROWS 8

/* Reserve a row count matching the picture's aspect ratio (a terminal cell is
 * about twice as tall as wide, so a square image needs roughly half as many
 * rows as columns). Falls back to a default when the file can't be measured. */
static int image_rows(R *r, const char *src) {
    char *path = path_resolve(r->basedir, src);
    int rows = IMG_ROWS, iw, ih;
    if (path && img_pixel_size(path, &iw, &ih) && iw > 0 && ih > 0) {
        int target = r->width > 72 ? 72 : r->width;            /* cap display width */
        int rr = (int)((double)ih / iw * target * 0.5 + 0.5);  /* 0.5 = cell w/h */
        rows = rr < 3 ? 3 : (rr > 20 ? 20 : rr);
    }
    free(path);
    return rows;
}

/* Accumulate an image's alt text (it arrives as text events inside the span). */
static void img_alt_append(R *r, const char *s, size_t n) {
    if (r->img_alt_len + n + 1 > r->img_alt_cap) {
        size_t nc = r->img_alt_cap ? r->img_alt_cap : 64;
        while (r->img_alt_len + n + 1 > nc) nc *= 2;
        char *p = realloc(r->img_alt, nc);
        if (!p) return;
        r->img_alt = p; r->img_alt_cap = nc;
    }
    memcpy(r->img_alt + r->img_alt_len, s, n);
    r->img_alt_len += n;
}

/* Emit the placeholder line (▦ alt, linked to the source so Enter opens it)
 * and the reserved blank rows that give the picture somewhere to be drawn. */
static void emit_image(R *r) {
    flush_word(r);
    if (r->line_has) line_commit(r);
    line_start_indent(r);
    Style is; memset(&is, 0, sizeof is);
    is.has_fg = 1; is.fg = link_fg(r->theme); is.underline = 1;
    runs_push(&r->line, &r->line_n, &r->line_cap, "\xe2\x96\xa6 ", 4, is);   /* ▦ */
    const char *label = r->img_alt_len ? r->img_alt : (r->img_src ? r->img_src : "image");
    size_t ll = r->img_alt_len ? r->img_alt_len : strlen(label);
    runs_push(&r->line, &r->line_n, &r->line_cap, label, ll, is);
    if (r->img_src) {
        r->line[r->line_n - 1].link = strdup(r->img_src);   /* Enter opens it */
        r->line[r->line_n - 2].link = strdup(r->img_src);
    }
    r->line_cols += 2 + u8_width(label, ll);
    r->line_has = 1;
    line_commit(r);
    int rows = r->img_src ? image_rows(r, r->img_src) : IMG_ROWS;
    Line *L = &r->doc->lines[r->doc->nline - 1];
    L->image = r->img_src ? strdup(r->img_src) : NULL;
    L->img_rows = rows;
    for (int i = 1; i < rows; i++) line_commit(r);           /* reserved rows */
    r->img_alt_len = 0;
}

/* ---- md4c callbacks ------------------------------------------------------- */
/* md4c invokes enter/leave_block, enter/leave_span, and text in document order;
 * each one mutates the renderer state R and emits runs/lines. */

/* Emit a pending list item's bullet ("•") or number ("N.") before its text. */
static void li_marker(R *r) {
    if (!r->li_pending) return;
    r->li_pending = 0;
    int d = r->list_depth - 1;
    char buf[32];
    Style ms; memset(&ms, 0, sizeof ms);
    ms.has_fg = 1; ms.fg = accent_fg(r->theme);
    if (d >= 0 && d < MAX_LIST && r->list_mark[d]) {
        snprintf(buf, sizeof buf, "%d.", r->ol_next[d]++);
        word_run(r, buf, strlen(buf), ms);
    } else {
        word_run(r, "\xe2\x80\xa2", 3, ms);   /* • */
    }
    Style plain; memset(&plain, 0, sizeof plain);
    word_run(r, " ", 1, plain);
}

/* Open a block: set up styling, indentation, list/quote nesting, or a rule. */
static int cb_enter_block(MD_BLOCKTYPE type, void *detail, void *ud) {
    R *r = ud;
    switch (type) {
        case MD_BLOCK_DOC: break;
        case MD_BLOCK_H: {
            MD_BLOCK_H_DETAIL *d = detail;
            r->heading = (int)d->level;
            r->heading_start = (int)r->doc->nline;  /* first heading line index */
            style_push(r);
            r->cur.bold = 1; r->cur.has_fg = 1;
            r->cur.fg = heading_fg(r->theme, r->heading);
            break;
        }
        case MD_BLOCK_P: break;
        case MD_BLOCK_QUOTE: r->quote_depth++; break;
        case MD_BLOCK_UL:
            flush_word(r);
            if (r->line_has) line_commit(r);
            if (r->list_depth < MAX_LIST) r->list_mark[r->list_depth] = 0;
            r->list_depth++;
            break;
        case MD_BLOCK_OL: {
            MD_BLOCK_OL_DETAIL *d = detail;
            flush_word(r);
            if (r->line_has) line_commit(r);
            if (r->list_depth < MAX_LIST) {
                r->list_mark[r->list_depth] = 1;
                r->ol_next[r->list_depth] = (int)d->start;
            }
            r->list_depth++;
            break;
        }
        case MD_BLOCK_LI: r->li_pending = 1; break;
        case MD_BLOCK_HR: {
            Style rs; memset(&rs, 0, sizeof rs);
            rs.has_fg = 1; rs.fg = rule_fg(r->theme); rs.dim = 1;
            char bar[3 * 256]; int w = r->width; if (w > 256) w = 256;
            int p = 0;
            for (int i = 0; i < w; i++) { memcpy(bar + p, "\xe2\x94\x80", 3); p += 3; }
            runs_push(&r->line, &r->line_n, &r->line_cap, bar, p, rs);
            r->line_cols = w; r->line_has = 1;
            line_commit(r);
            break;
        }
        case MD_BLOCK_CODE: {
            MD_BLOCK_CODE_DETAIL *d = detail;
            r->in_code = 1; r->code_len = 0;
            memset(&r->hl, 0, sizeof r->hl);
            r->code_lang = (d && d->lang.text) ? hl_lang(d->lang.text, d->lang.size) : -1;
            break;
        }
        case MD_BLOCK_TABLE: {
            MD_BLOCK_TABLE_DETAIL *d = detail;
            flush_word(r);
            if (r->line_has) line_commit(r);
            r->in_table = 1; r->tbl_nrow = 0; r->tbl_col = 0;
            r->tbl_ncol = d ? (int)d->col_count : 0;
            if (r->tbl_ncol > MAX_COLS) r->tbl_ncol = MAX_COLS;
            for (int c = 0; c < MAX_COLS; c++) r->tbl_align[c] = MD_ALIGN_DEFAULT;
            break;
        }
        case MD_BLOCK_THEAD: r->tbl_header = 1; break;
        case MD_BLOCK_TBODY: r->tbl_header = 0; break;
        case MD_BLOCK_TR: {
            TRow *row = tbl_currow(r);
            if (row) { memset(row, 0, sizeof *row); row->header = r->tbl_header; }
            r->tbl_col = 0;
            break;
        }
        case MD_BLOCK_TH:
        case MD_BLOCK_TD: {
            MD_BLOCK_TD_DETAIL *d = detail;
            if (d && r->tbl_col >= 0 && r->tbl_col < MAX_COLS) r->tbl_align[r->tbl_col] = d->align;
            style_push(r);
            if (type == MD_BLOCK_TH) r->cur.bold = 1;
            r->in_cell = 1;
            break;
        }
        default: break;
    }
    return 0;
}

/* Close a block: flush its line(s) and add the trailing blank line / spacing. */
static int cb_leave_block(MD_BLOCKTYPE type, void *detail, void *ud) {
    R *r = ud;
    (void)detail;
    switch (type) {
        case MD_BLOCK_DOC: break;
        case MD_BLOCK_H:
            flush_word(r);
            style_pop(r);
            if (r->line_has) line_commit(r);
            /* tag the heading's first line so the TOC can find it */
            if (r->heading_start < (int)r->doc->nline)
                r->doc->lines[r->heading_start].heading = r->heading;
            line_commit(r);            /* blank line after heading */
            r->heading = 0;
            break;
        case MD_BLOCK_P:
            flush_word(r);
            if (r->line_has) line_commit(r);
            line_commit(r);            /* blank line after paragraph */
            break;
        case MD_BLOCK_QUOTE:
            if (r->quote_depth) r->quote_depth--;
            break;
        case MD_BLOCK_UL:
        case MD_BLOCK_OL:
            if (r->list_depth) r->list_depth--;
            if (r->list_depth == 0) line_commit(r);
            break;
        case MD_BLOCK_LI:
            flush_word(r);
            if (r->line_has) line_commit(r);
            r->li_pending = 0;
            break;
        case MD_BLOCK_HR: break;
        case MD_BLOCK_CODE: {
            size_t n = r->code_len;
            const char *b = r->code_buf ? r->code_buf : "";
            size_t start = 0;
            for (size_t i = 0; i < n; i++) {
                if (b[i] == '\n') { code_line(r, b + start, i - start); start = i + 1; }
            }
            if (start < n) code_line(r, b + start, n - start);
            r->in_code = 0;
            line_commit(r);            /* blank line after code block */
            break;
        }
        case MD_BLOCK_TH:
        case MD_BLOCK_TD:
            style_pop(r);
            r->in_cell = 0;
            if (r->tbl_nrow < r->tbl_rowcap && r->tbl_col < MAX_COLS)
                r->tbl_rows[r->tbl_nrow].ncell = r->tbl_col + 1;
            r->tbl_col++;
            break;
        case MD_BLOCK_THEAD:
        case MD_BLOCK_TBODY:
            break;
        case MD_BLOCK_TR:
            r->tbl_nrow++;
            break;
        case MD_BLOCK_TABLE:
            table_emit(r);
            table_free(r);
            line_commit(r);            /* blank line after table */
            break;
        default: break;
    }
    return 0;
}

/* Open an inline span: push the style and apply the span's emphasis/colour. */
static int cb_enter_span(MD_SPANTYPE type, void *detail, void *ud) {
    R *r = ud;
    (void)detail;
    style_push(r);
    switch (type) {
        case MD_SPAN_STRONG: r->cur.bold = 1; break;
        case MD_SPAN_EM:     r->cur.italic = 1; break;
        case MD_SPAN_DEL:    r->cur.strike = 1; break;
        case MD_SPAN_U:      r->cur.underline = 1; break;
        case MD_SPAN_CODE:
            r->cur.has_fg = 1; r->cur.fg = code_fg(r->theme);
            r->cur.has_bg = 1; r->cur.bg = code_bg(r->theme);
            break;
        case MD_SPAN_A: {
            MD_SPAN_A_DETAIL *d = detail;
            free(r->cur_link);
            r->cur_link = d->href.size ? strndup(d->href.text, d->href.size) : NULL;
            r->cur.underline = 1; r->cur.has_fg = 1; r->cur.fg = link_fg(r->theme);
            break;
        }
        case MD_SPAN_WIKILINK: {
            MD_SPAN_WIKILINK_DETAIL *d = detail;
            free(r->cur_link);
            r->cur_link = d->target.size ? strndup(d->target.text, d->target.size) : NULL;
            r->cur.underline = 1; r->cur.has_fg = 1; r->cur.fg = link_fg(r->theme);
            break;
        }
        case MD_SPAN_IMG: {
            MD_SPAN_IMG_DETAIL *d = detail;
            free(r->img_src);
            r->img_src = d->src.size ? strndup(d->src.text, d->src.size) : NULL;
            r->img_alt_len = 0;
            r->in_image = 1;        /* alt text now flows into img_alt, not the line */
            break;
        }
        default: break;
    }
    return 0;
}

/* Close an inline span: restore the style; clear the link when leaving one. */
static int cb_leave_span(MD_SPANTYPE type, void *detail, void *ud) {
    R *r = ud;
    (void)detail;
    if (type == MD_SPAN_A || type == MD_SPAN_WIKILINK) { free(r->cur_link); r->cur_link = NULL; }
    if (type == MD_SPAN_IMG) {
        emit_image(r);
        free(r->img_src); r->img_src = NULL;
        r->in_image = 0;
    }
    style_pop(r);
    return 0;
}

/* Handle a run of text: buffer code, wrap normal text, or break lines. */
static int cb_text(MD_TEXTTYPE type, const MD_CHAR *text, MD_SIZE size, void *ud) {
    R *r = ud;
    if (r->in_image) {   /* an image's alt text: collect it, don't lay it out */
        if (type == MD_TEXT_NORMAL || type == MD_TEXT_ENTITY || type == MD_TEXT_CODE)
            img_alt_append(r, text, size);
        return 0;
    }
    switch (type) {
        case MD_TEXT_CODE:
            if (r->in_code)      code_buf_append(r, text, size);
            else if (r->in_cell) cell_push(r, text, size, r->cur);
            else { li_marker(r); word_run(r, text, size, r->cur); }
            break;
        case MD_TEXT_NORMAL:
        case MD_TEXT_ENTITY:
            if (r->in_cell) cell_push(r, text, size, r->cur);
            else { li_marker(r); feed_text(r, text, size); }
            break;
        case MD_TEXT_BR:
        case MD_TEXT_SOFTBR:
            if (r->in_cell) cell_push(r, " ", 1, r->cur);   /* cells don't wrap */
            else { flush_word(r); if (type == MD_TEXT_BR && r->line_has) line_commit(r); }
            break;
        case MD_TEXT_NULLCHAR:
            word_run(r, "\xef\xbf\xbd", 3, r->cur);  /* U+FFFD */
            break;
        default: break;
    }
    return 0;
}

/* ---- source-line attribution ---------------------------------------------- */
/* md4c reports no source byte-offsets, so to map the cursor between the reader
 * and the editor we attribute each visual line back to a source line by content.
 * For each Doc line we take the first short word and scan forward (never back)
 * through the original source for the line that contains it. It is exact for
 * structural lines (headings, code, list items, table rows, single-line
 * paragraphs) and monotonic everywhere, which beats a purely proportional map. */

/* First word of a Doc line, lowercased, up to cap-1 bytes: a token starts on an
 * ASCII letter/digit (so decoration glyphs like • │ never start one) and may
 * continue into UTF-8 bytes (so accented words still match). 0 = no word. */
static int doc_line_key(const Line *L, char *key, int cap) {
    int kn = 0, started = 0;
    for (size_t r = 0; r < L->nrun; r++) {
        const unsigned char *t = (const unsigned char *)L->runs[r].text;
        for (size_t i = 0; i < L->runs[r].len; i++) {
            unsigned char c = t[i];
            int start_ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
            if (!started) { if (!start_ok) continue; started = 1; }
            else if (!(start_ok || c >= 0x80)) { goto done; }
            key[kn++] = (char)((c >= 'A' && c <= 'Z') ? c + 32 : c);
            if (kn >= cap - 1) goto done;
        }
    }
done:
    key[kn] = '\0';
    return kn;
}

/* Case-insensitive substring test (key is already lowercase). */
static int ci_contains(const char *hay, size_t hn, const char *key, size_t kn) {
    if (kn == 0 || kn > hn) return 0;
    for (size_t i = 0; i + kn <= hn; i++) {
        size_t j = 0;
        for (; j < kn; j++) {
            char a = hay[i + j];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (a != key[j]) break;
        }
        if (j == kn) return 1;
    }
    return 0;
}

/* Fill each Line.source_line (1-based) by matching against the original src. */
static void tag_source_lines(Doc *d, const char *src, size_t len) {
    if (!d || !src) return;
    size_t nsrc = 1;
    for (size_t i = 0; i < len; i++) if (src[i] == '\n') nsrc++;
    struct { const char *p; size_t n; } *sl = malloc(nsrc * sizeof *sl);
    if (!sl) return;
    size_t k = 0, st = 0;
    for (size_t i = 0; i <= len; i++)
        if (i == len || src[i] == '\n') { sl[k].p = src + st; sl[k].n = i - st; k++; st = i + 1; }

    size_t si = 0; int last = 0;
    for (size_t li = 0; li < d->nline; li++) {
        char key[5];
        int kn = doc_line_key(&d->lines[li], key, (int)sizeof key);
        if (kn == 0) { d->lines[li].source_line = 0; continue; }   /* blank/rule line */
        size_t j = si;
        for (; j < k; j++) if (ci_contains(sl[j].p, sl[j].n, key, (size_t)kn)) break;
        if (j < k) { d->lines[li].source_line = (int)j + 1; si = j + 1; last = (int)j + 1; }
        else        d->lines[li].source_line = last ? last : (si < k ? (int)si + 1 : (int)k);
    }
    free(sl);
}

/* ---- entry / teardown ----------------------------------------------------- */

/* render_doc with basedir = NULL; see render.h. */
Doc *render_doc(const char *src, size_t len, int width, int dark) {
    return render_doc_at(src, len, width, dark, NULL);
}

/* Shim: map the dark flag to the matching auto theme. See render.h. */
Doc *render_doc_at(const char *src, size_t len, int width, int dark, const char *basedir) {
    return render_doc_themed(src, len, width, theme_auto(dark), basedir);
}

/* Parse src with md4c (GFM dialect) and build the Doc with `theme`; see render.h. */
Doc *render_doc_themed(const char *src, size_t len, int width,
                       const Theme *theme, const char *basedir) {
    if (!theme) theme = theme_auto(1);
    Doc *doc = calloc(1, sizeof *doc);
    if (!doc) return NULL;
    doc->width = width > 4 ? width : 80;

    R r;
    memset(&r, 0, sizeof r);
    r.doc = doc;
    r.width = doc->width;
    r.theme = theme;
    r.basedir = basedir;

    MD_PARSER parser;
    memset(&parser, 0, sizeof parser);
    parser.abi_version = 0;
    parser.flags = MD_DIALECT_GITHUB | MD_FLAG_PERMISSIVEATXHEADERS | MD_FLAG_WIKILINKS;
    parser.enter_block = cb_enter_block;
    parser.leave_block = cb_leave_block;
    parser.enter_span  = cb_enter_span;
    parser.leave_span  = cb_leave_span;
    parser.text        = cb_text;

    /* apply tolerant-Markdown fix-ups, then parse the result */
    size_t plen = len;
    char *pre = preprocess(src, len, &plen);
    int rc = md_parse(pre ? pre : src, (MD_SIZE)(pre ? plen : len), &parser, &r);
    free(pre);
    flush_word(&r);
    if (r.line_has || r.line_n > 0) line_commit(&r);

    /* free transient buffers */
    for (size_t i = 0; i < r.word_n; i++) { free(r.word[i].text); free(r.word[i].link); }
    free(r.word);
    free(r.line);
    free(r.code_buf);
    free(r.cur_link);
    free(r.img_src);
    free(r.img_alt);
    table_free(&r);            /* no-op unless a table was left unfinished */

    if (rc != 0) { doc_free(doc); return NULL; }
    tag_source_lines(doc, src, len);   /* map visual lines back to source lines */
    return doc;
}

/* Free a Doc and all the runs it owns. */
void doc_free(Doc *d) {
    if (!d) return;
    for (size_t i = 0; i < d->nline; i++) {
        Line *L = &d->lines[i];
        for (size_t j = 0; j < L->nrun; j++) { free(L->runs[j].text); free(L->runs[j].link); }
        free(L->runs);
        free(L->image);
    }
    free(d->lines);
    free(d);
}
