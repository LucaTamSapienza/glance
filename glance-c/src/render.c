/* render.c — glance's Markdown renderer: md4c → structured Doc.
 *
 * md4c parses; the callbacks here build a Doc (lines of styled runs). Inline
 * flow is word-wrapped to the target width. Block constructs (headings, lists,
 * quotes, code fences, tables, rules) get their own styling. A Style stack
 * tracks nested inline emphasis so leaving a span restores exactly the style
 * that was active before it (including a heading's colour).
 */
#include "render.h"

#include <md4c.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- display width (slice-1/2 approximation) ------------------------------ */

static int utf8_is_cont(unsigned char c) { return (c & 0xC0) == 0x80; }

static int vis_cols(const char *s, size_t n) {
    int cols = 0;
    for (size_t i = 0; i < n; i++)
        if (!utf8_is_cont((unsigned char)s[i])) cols++;
    return cols;
}

/* ---- palette -------------------------------------------------------------- */

static RGB rgb(uint8_t r, uint8_t g, uint8_t b) { RGB c = {r, g, b}; return c; }

static RGB heading_fg(int dark, int level) {
    if (dark) switch (level) {
        case 1:  return rgb(255, 135, 255);
        case 2:  return rgb(95, 215, 255);
        case 3:  return rgb(135, 215, 135);
        case 4:  return rgb(255, 215, 135);
        default: return rgb(160, 160, 160);
    } else switch (level) {
        case 1:  return rgb(175, 0, 175);
        case 2:  return rgb(0, 135, 175);
        case 3:  return rgb(0, 135, 0);
        case 4:  return rgb(175, 95, 0);
        default: return rgb(96, 96, 96);
    }
}
static RGB code_bg(int dark)  { return dark ? rgb(48, 48, 48)   : rgb(228, 228, 228); }
static RGB code_fg(int dark)  { return dark ? rgb(208, 208, 208): rgb(135, 0, 0); }
static RGB link_fg(int dark)  { return dark ? rgb(95, 175, 255) : rgb(0, 95, 215); }
static RGB accent_fg(int dark){ return dark ? rgb(95, 215, 255) : rgb(0, 135, 175); }
static RGB quote_fg(int dark) { return dark ? rgb(120, 120, 120): rgb(140, 140, 140); }
static RGB rule_fg(int dark)  { return dark ? rgb(88, 88, 88)   : rgb(170, 170, 170); }

/* ---- renderer state ------------------------------------------------------- */

#define MAX_LIST  16
#define MAX_STYLE 32

typedef struct {
    Doc  *doc;
    int   width, dark;

    /* pending visual line being assembled */
    Run  *line; size_t line_n, line_cap;
    int   line_cols, line_has;

    /* pending word (may hold >1 run if styles change mid-word) */
    Run  *word; size_t word_n, word_cap;
    int   word_cols;

    /* current inline style + a save stack for nested spans/blocks */
    Style cur;
    Style stack[MAX_STYLE]; int sp;

    /* block context */
    int    in_code;
    char  *code_buf; size_t code_len, code_cap;

    int    quote_depth;
    int    list_depth;
    char   list_mark[MAX_LIST];
    int    ol_next[MAX_LIST];
    int    li_pending;
    int    heading;
} R;

/* ---- run / line builders -------------------------------------------------- */

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
    (*n)++;
    return 0;
}

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
    L->fill = 0; L->fill_bg = rgb(0, 0, 0);
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
        q.has_fg = 1; q.fg = quote_fg(r->dark); q.dim = 1;
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
        free(w->text);
    }
    r->line_cols += r->word_cols;
    r->line_has = 1;
    r->word_n = 0;
    r->word_cols = 0;
}

/* push a styled run onto the pending word */
static void word_run(R *r, const char *text, size_t len, Style st) {
    runs_push(&r->word, &r->word_n, &r->word_cap, text, len, st);
    r->word_cols += vis_cols(text, len);
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

/* emit one code-block line: indent + styled text + bg fill to width */
static void code_line(R *r, const char *s, size_t n) {
    line_start_indent(r);
    Style cs; memset(&cs, 0, sizeof cs);
    cs.has_fg = 1; cs.fg = code_fg(r->dark);
    cs.has_bg = 1; cs.bg = code_bg(r->dark);
    /* leading space inside the box */
    runs_push(&r->line, &r->line_n, &r->line_cap, " ", 1, cs);
    runs_push(&r->line, &r->line_n, &r->line_cap, s, n, cs);
    r->line_cols += 1 + vis_cols(s, n);
    /* mark the line for bg fill to the right edge */
    r->line_has = 1;
    line_commit(r);
    Line *L = &r->doc->lines[r->doc->nline - 1];
    L->fill = 1; L->fill_bg = code_bg(r->dark);
}

/* ---- md4c callbacks ------------------------------------------------------- */

static void li_marker(R *r) {
    if (!r->li_pending) return;
    r->li_pending = 0;
    int d = r->list_depth - 1;
    char buf[32];
    Style ms; memset(&ms, 0, sizeof ms);
    ms.has_fg = 1; ms.fg = accent_fg(r->dark);
    if (d >= 0 && d < MAX_LIST && r->list_mark[d]) {
        snprintf(buf, sizeof buf, "%d.", r->ol_next[d]++);
        word_run(r, buf, strlen(buf), ms);
    } else {
        word_run(r, "\xe2\x80\xa2", 3, ms);   /* • */
    }
    Style plain; memset(&plain, 0, sizeof plain);
    word_run(r, " ", 1, plain);
}

static int cb_enter_block(MD_BLOCKTYPE type, void *detail, void *ud) {
    R *r = ud;
    switch (type) {
        case MD_BLOCK_DOC: break;
        case MD_BLOCK_H: {
            MD_BLOCK_H_DETAIL *d = detail;
            r->heading = (int)d->level;
            style_push(r);
            r->cur.bold = 1; r->cur.has_fg = 1;
            r->cur.fg = heading_fg(r->dark, r->heading);
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
            rs.has_fg = 1; rs.fg = rule_fg(r->dark); rs.dim = 1;
            char bar[3 * 256]; int w = r->width; if (w > 256) w = 256;
            int p = 0;
            for (int i = 0; i < w; i++) { memcpy(bar + p, "\xe2\x94\x80", 3); p += 3; }
            runs_push(&r->line, &r->line_n, &r->line_cap, bar, p, rs);
            r->line_cols = w; r->line_has = 1;
            line_commit(r);
            break;
        }
        case MD_BLOCK_CODE:
            r->in_code = 1; r->code_len = 0;
            break;
        case MD_BLOCK_TH:
        case MD_BLOCK_TD:
            style_push(r); r->cur.bold = 1;
            break;
        default: break;
    }
    return 0;
}

static int cb_leave_block(MD_BLOCKTYPE type, void *detail, void *ud) {
    R *r = ud;
    (void)detail;
    switch (type) {
        case MD_BLOCK_DOC: break;
        case MD_BLOCK_H:
            flush_word(r);
            style_pop(r);
            if (r->line_has) line_commit(r);
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
            flush_word(r);
            style_pop(r);
            { Style plain; memset(&plain, 0, sizeof plain);
              runs_push(&r->line, &r->line_n, &r->line_cap, "  ", 2, plain);
              r->line_cols += 2; }
            break;
        case MD_BLOCK_TR:
            if (r->line_has) line_commit(r);
            break;
        case MD_BLOCK_TABLE:
            line_commit(r);
            break;
        default: break;
    }
    return 0;
}

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
            r->cur.has_fg = 1; r->cur.fg = code_fg(r->dark);
            r->cur.has_bg = 1; r->cur.bg = code_bg(r->dark);
            break;
        case MD_SPAN_A:
        case MD_SPAN_WIKILINK:
            r->cur.underline = 1; r->cur.has_fg = 1; r->cur.fg = link_fg(r->dark);
            break;
        default: break;
    }
    return 0;
}

static int cb_leave_span(MD_SPANTYPE type, void *detail, void *ud) {
    R *r = ud;
    (void)detail; (void)type;
    style_pop(r);
    return 0;
}

static int cb_text(MD_TEXTTYPE type, const MD_CHAR *text, MD_SIZE size, void *ud) {
    R *r = ud;
    switch (type) {
        case MD_TEXT_CODE:
            if (r->in_code) {
                code_buf_append(r, text, size);
            } else {
                li_marker(r);
                word_run(r, text, size, r->cur);
            }
            break;
        case MD_TEXT_NORMAL:
        case MD_TEXT_ENTITY:
            li_marker(r);
            feed_text(r, text, size);
            break;
        case MD_TEXT_BR:
            flush_word(r);
            if (r->line_has) line_commit(r);
            break;
        case MD_TEXT_SOFTBR:
            flush_word(r);
            break;
        case MD_TEXT_NULLCHAR:
            word_run(r, "\xef\xbf\xbd", 3, r->cur);  /* U+FFFD */
            break;
        default: break;
    }
    return 0;
}

/* ---- entry / teardown ----------------------------------------------------- */

Doc *render_doc(const char *src, size_t len, int width, int dark) {
    Doc *doc = calloc(1, sizeof *doc);
    if (!doc) return NULL;
    doc->width = width > 4 ? width : 80;

    R r;
    memset(&r, 0, sizeof r);
    r.doc = doc;
    r.width = doc->width;
    r.dark = dark;

    MD_PARSER parser;
    memset(&parser, 0, sizeof parser);
    parser.abi_version = 0;
    parser.flags = MD_DIALECT_GITHUB | MD_FLAG_PERMISSIVEATXHEADERS;
    parser.enter_block = cb_enter_block;
    parser.leave_block = cb_leave_block;
    parser.enter_span  = cb_enter_span;
    parser.leave_span  = cb_leave_span;
    parser.text        = cb_text;

    int rc = md_parse(src, (MD_SIZE)len, &parser, &r);
    flush_word(&r);
    if (r.line_has || r.line_n > 0) line_commit(&r);

    /* free transient buffers */
    for (size_t i = 0; i < r.word_n; i++) free(r.word[i].text);
    free(r.word);
    free(r.line);
    free(r.code_buf);

    if (rc != 0) { doc_free(doc); return NULL; }
    return doc;
}

void doc_free(Doc *d) {
    if (!d) return;
    for (size_t i = 0; i < d->nline; i++) {
        Line *L = &d->lines[i];
        for (size_t j = 0; j < L->nrun; j++) free(L->runs[j].text);
        free(L->runs);
    }
    free(d->lines);
    free(d);
}
