/* render_md.c — glance's own Markdown→ANSI renderer.
 *
 * md4c parses the document and drives the callbacks below. Each callback emits
 * ANSI directly, so we own every byte of the output (unlike glamour, which
 * handed back an opaque string we then had to regex). Inline flow text is
 * word-wrapped to a target width; block constructs (headings, lists, quotes,
 * code fences, tables, rules) get their own styling.
 *
 * This is slice 1: a stdout renderer with no TUI dependency. The structured
 * line model and source-line map come in a later slice; here we emit a single
 * ANSI string, which is enough to validate that our rendering matches what we
 * want before notcurses enters the picture.
 */
#include "render_md.h"

#include <md4c.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- dynamic string buffer ------------------------------------------------ */

typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} SB;

static int sb_grow(SB *sb, size_t need) {
    if (sb->len + need + 1 <= sb->cap) return 0;
    size_t cap = sb->cap ? sb->cap : 256;
    while (sb->len + need + 1 > cap) cap *= 2;
    char *p = realloc(sb->data, cap);
    if (!p) return -1;
    sb->data = p;
    sb->cap = cap;
    return 0;
}

static void sb_putn(SB *sb, const char *s, size_t n) {
    if (sb_grow(sb, n)) return;
    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
}

static void sb_puts(SB *sb, const char *s) { sb_putn(sb, s, strlen(s)); }
static void sb_putc(SB *sb, char c) { sb_putn(sb, &c, 1); }

/* ---- display-width helpers (slice-1 approximation) ------------------------ */

/* Count display columns of a UTF-8 byte: lead bytes count 1, continuation
 * bytes (0b10xxxxxx) count 0. Wide CJK/emoji are approximated as 1 here;
 * notcurses' real cell width takes over in the TUI slice. */
static int utf8_is_cont(unsigned char c) { return (c & 0xC0) == 0x80; }

static int vis_cols(const char *s, size_t n) {
    int cols = 0;
    for (size_t i = 0; i < n; i++)
        if (!utf8_is_cont((unsigned char)s[i])) cols++;
    return cols;
}

/* ---- renderer state ------------------------------------------------------- */

#define MAX_LIST 16

typedef struct {
    SB     out;
    int    width;
    int    dark;

    /* pending word + current visual line, for word-wrapping inline flow */
    SB     word;        /* raw bytes incl. ANSI; cols tracked separately */
    int    word_cols;
    int    line_cols;   /* visible cols already on the current visual line */
    int    line_has;    /* line has printable content (for soft spaces) */

    /* block context */
    int    in_code;     /* inside a fenced/indented code block */
    SB     code_buf;    /* raw code-block text, split into lines on leave */
    int    quote_depth;
    int    list_depth;
    char   list_mark[MAX_LIST];   /* 0 = ul, else the running ol counter base */
    int    ol_next[MAX_LIST];     /* next ordered-list number at this depth */
    int    li_pending;            /* a list item just opened; emit marker on first text */
    int    heading;               /* current heading level, 0 if none */
} R;

/* indentation (in columns) contributed by the active block nesting */
static int block_indent(R *r) {
    return r->quote_depth * 2 + r->list_depth * 2;
}

/* ---- ANSI palette --------------------------------------------------------- */

static const char *RESET = "\033[0m";

static const char *heading_sgr(R *r, int level) {
    if (r->dark) {
        switch (level) {
            case 1: return "\033[1;38;5;213m"; /* bright pink   */
            case 2: return "\033[1;38;5;81m";  /* cyan          */
            case 3: return "\033[1;38;5;114m"; /* green         */
            case 4: return "\033[1;38;5;222m"; /* gold          */
            default:return "\033[1;38;5;245m"; /* grey          */
        }
    } else {
        switch (level) {
            case 1: return "\033[1;38;5;127m";
            case 2: return "\033[1;38;5;31m";
            case 3: return "\033[1;38;5;28m";
            case 4: return "\033[1;38;5;130m";
            default:return "\033[1;38;5;240m";
        }
    }
}

/* ---- inline flow: word buffering + wrapping ------------------------------- */

static void emit_indent(R *r) {
    int n = block_indent(r);
    for (int i = 0; i < n; i++) sb_putc(&r->out, ' ');
    if (r->quote_depth > 0) {
        /* draw a quote bar at the left of the quoted region */
        sb_puts(&r->out, r->dark ? "\033[38;5;240m│ \033[0m"
                                  : "\033[38;5;245m│ \033[0m");
    }
    r->line_cols = block_indent(r) + (r->quote_depth ? 2 : 0);
}

/* start a fresh visual line within the current block */
static void newline(R *r) {
    sb_putc(&r->out, '\n');
    r->line_has = 0;
    r->line_cols = 0;
}

/* flush the pending word onto the current visual line, wrapping if needed */
static void flush_word(R *r) {
    if (r->word.len == 0) return;
    int avail = r->width;
    int need = r->word_cols + (r->line_has ? 1 : 0);
    if (r->line_has && r->line_cols + need > avail) {
        newline(r);
    }
    if (!r->line_has) {
        emit_indent(r);
    } else {
        sb_putc(&r->out, ' ');
        r->line_cols += 1;
    }
    sb_putn(&r->out, r->word.data, r->word.len);
    r->line_cols += r->word_cols;
    r->line_has = 1;
    r->word.len = 0;
    if (r->word.data) r->word.data[0] = '\0';
    r->word_cols = 0;
}

/* append raw (already-styled) bytes to the pending word, with col width */
static void word_putn(R *r, const char *s, size_t n, int cols) {
    sb_putn(&r->word, s, n);
    r->word_cols += cols;
}
static void word_ansi(R *r, const char *sgr) { sb_puts(&r->word, sgr); }

/* feed normal inline text, splitting on spaces into wrappable words */
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
        word_putn(r, s + start, i - start, vis_cols(s + start, i - start));
    }
}

/* ---- md4c callbacks ------------------------------------------------------- */

static void code_line(R *r, const char *s, size_t n);  /* defined below */

static void li_marker(R *r) {
    if (!r->li_pending) return;
    r->li_pending = 0;
    int d = r->list_depth - 1;
    char buf[32];
    if (d >= 0 && d < MAX_LIST && r->list_mark[d]) {
        snprintf(buf, sizeof buf, "%d. ", r->ol_next[d]++);
    } else {
        snprintf(buf, sizeof buf, "%s ", r->dark ? "•" : "•");
    }
    /* the marker is part of the first word so wrapping keeps it attached */
    word_ansi(r, r->dark ? "\033[38;5;81m" : "\033[38;5;31m");
    word_putn(r, buf, strlen(buf), vis_cols(buf, strlen(buf)));
    word_ansi(r, RESET);
}

static int cb_enter_block(MD_BLOCKTYPE type, void *detail, void *ud) {
    R *r = ud;
    switch (type) {
        case MD_BLOCK_DOC: break;
        case MD_BLOCK_H: {
            MD_BLOCK_H_DETAIL *d = detail;
            r->heading = (int)d->level;
            word_ansi(r, heading_sgr(r, r->heading));
            break;
        }
        case MD_BLOCK_P: break;
        case MD_BLOCK_QUOTE: r->quote_depth++; break;
        case MD_BLOCK_UL:
            /* a nested list inside an item must break the item's own line */
            flush_word(r);
            if (r->line_has) newline(r);
            if (r->list_depth < MAX_LIST) r->list_mark[r->list_depth] = 0;
            r->list_depth++;
            break;
        case MD_BLOCK_OL: {
            MD_BLOCK_OL_DETAIL *d = detail;
            flush_word(r);
            if (r->line_has) newline(r);
            if (r->list_depth < MAX_LIST) {
                r->list_mark[r->list_depth] = 1;
                r->ol_next[r->list_depth] = (int)d->start;
            }
            r->list_depth++;
            break;
        }
        case MD_BLOCK_LI: r->li_pending = 1; break;
        case MD_BLOCK_HR: {
            for (int i = 0; i < r->width; i++) sb_puts(&r->out, "─");
            newline(r);
            break;
        }
        case MD_BLOCK_CODE: {
            r->in_code = 1;
            r->code_buf.len = 0;
            if (r->code_buf.data) r->code_buf.data[0] = '\0';
            break;
        }
        case MD_BLOCK_TABLE: break;
        case MD_BLOCK_TR: break;
        case MD_BLOCK_TH:
        case MD_BLOCK_TD: word_ansi(r, "\033[1m"); break;
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
            word_ansi(r, RESET);
            flush_word(r);
            newline(r);
            newline(r);                 /* blank line after heading */
            r->heading = 0;
            break;
        case MD_BLOCK_P:
            flush_word(r);
            if (r->line_has) newline(r);
            newline(r);                 /* blank line after paragraph */
            break;
        case MD_BLOCK_QUOTE:
            if (r->quote_depth) r->quote_depth--;
            break;
        case MD_BLOCK_UL:
        case MD_BLOCK_OL:
            if (r->list_depth) r->list_depth--;
            if (r->list_depth == 0) newline(r);
            break;
        case MD_BLOCK_LI:
            flush_word(r);
            if (r->line_has) newline(r);
            r->li_pending = 0;
            break;
        case MD_BLOCK_HR: newline(r); break;
        case MD_BLOCK_CODE: {
            /* split the buffered block into lines; drop the final empty line
             * left by the fence's trailing newline */
            size_t n = r->code_buf.len;
            const char *b = r->code_buf.data ? r->code_buf.data : "";
            size_t start = 0;
            for (size_t i = 0; i < n; i++) {
                if (b[i] == '\n') {
                    code_line(r, b + start, i - start);
                    start = i + 1;
                }
            }
            if (start < n) code_line(r, b + start, n - start);
            r->in_code = 0;
            newline(r);
            break;
        }
        case MD_BLOCK_TH:
        case MD_BLOCK_TD:
            word_ansi(r, RESET);
            flush_word(r);
            sb_puts(&r->out, "  ");
            break;
        case MD_BLOCK_TR:
            if (r->line_has) newline(r);
            break;
        case MD_BLOCK_TABLE: newline(r); break;
        default: break;
    }
    return 0;
}

static int cb_enter_span(MD_SPANTYPE type, void *detail, void *ud) {
    R *r = ud;
    (void)detail;
    switch (type) {
        case MD_SPAN_STRONG: word_ansi(r, "\033[1m"); break;
        case MD_SPAN_EM:     word_ansi(r, "\033[3m"); break;
        case MD_SPAN_DEL:    word_ansi(r, "\033[9m"); break;
        case MD_SPAN_U:      word_ansi(r, "\033[4m"); break;
        case MD_SPAN_CODE:
            word_ansi(r, r->dark ? "\033[48;5;236m\033[38;5;252m"
                                 : "\033[48;5;254m\033[38;5;88m");
            break;
        case MD_SPAN_A:
        case MD_SPAN_WIKILINK:
            word_ansi(r, r->dark ? "\033[4;38;5;75m" : "\033[4;38;5;26m");
            break;
        default: break;
    }
    return 0;
}

static int cb_leave_span(MD_SPANTYPE type, void *detail, void *ud) {
    R *r = ud;
    (void)detail;
    switch (type) {
        case MD_SPAN_STRONG:
        case MD_SPAN_EM:
        case MD_SPAN_DEL:
        case MD_SPAN_U:
        case MD_SPAN_CODE:
        case MD_SPAN_A:
        case MD_SPAN_WIKILINK:
            word_ansi(r, RESET);
            /* re-assert heading colour if we're inside a heading */
            if (r->heading) word_ansi(r, heading_sgr(r, r->heading));
            break;
        default: break;
    }
    return 0;
}

/* emit a code block line with background fill to the wrap width */
static void code_line(R *r, const char *s, size_t n) {
    emit_indent(r);
    sb_puts(&r->out, r->dark ? "\033[48;5;236m\033[38;5;252m"
                             : "\033[48;5;254m\033[38;5;88m");
    sb_putc(&r->out, ' ');
    sb_putn(&r->out, s, n);
    int used = 1 + vis_cols(s, n);
    for (int i = used; i < r->width; i++) sb_putc(&r->out, ' ');
    sb_puts(&r->out, RESET);
    newline(r);
}

static int cb_text(MD_TEXTTYPE type, const MD_CHAR *text, MD_SIZE size, void *ud) {
    R *r = ud;
    switch (type) {
        case MD_TEXT_CODE:
            if (r->in_code) {
                /* buffer verbatim; split into lines on leave_block (md4c may
                 * deliver content and newlines as separate callbacks) */
                sb_putn(&r->code_buf, text, size);
            } else {
                /* inline code span: glued into the current word */
                li_marker(r);
                word_putn(r, text, size, vis_cols(text, size));
            }
            break;
        case MD_TEXT_NORMAL:
        case MD_TEXT_ENTITY:
            li_marker(r);
            feed_text(r, text, size);
            break;
        case MD_TEXT_BR:
            flush_word(r);
            if (r->line_has) newline(r);
            break;
        case MD_TEXT_SOFTBR:
            flush_word(r);   /* soft break = word boundary; wrapping decides */
            break;
        case MD_TEXT_NULLCHAR:
            word_putn(r, "�", 3, 1);
            break;
        default: break;
    }
    return 0;
}

/* ---- entry point ---------------------------------------------------------- */

char *render_markdown(const char *src, size_t len, int width, int dark) {
    R r;
    memset(&r, 0, sizeof r);
    r.width = width > 4 ? width : 80;
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

    if (md_parse(src, (MD_SIZE)len, &parser, &r) != 0) {
        free(r.out.data);
        free(r.word.data);
        return NULL;
    }
    flush_word(&r);
    free(r.word.data);
    free(r.code_buf.data);
    if (!r.out.data) {
        r.out.data = calloc(1, 1);
    }
    return r.out.data;
}
