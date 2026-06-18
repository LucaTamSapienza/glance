/* doc_html.c — serialize a Markdown document to a self-contained HTML page.
 *
 * Unlike the other sinks (doc_ansi.c, the TUI blitter, the agent projections),
 * this one does NOT consume the visual Doc. That model is width-wrapped into
 * terminal lines, with heading chips and hard breaks, so projecting it would
 * bake the terminal layout into the page. HTML wants semantic, reflowable
 * structure, so the exporter re-runs md4c — the very parser the renderer uses —
 * with its own SAX callbacks and emits semantic HTML. It still reuses the
 * project's own pieces: the theme palette for colours and highlight.c for
 * fenced-code syntax colouring, so the page looks like glance with no
 * JavaScript and no external assets.
 */
#include "doc_html.h"
#include "render.h"
#include "highlight.h"

#include <md4c.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* A growable, NUL-terminated string builder (same shape as doc_ansi.c's). */
typedef struct { char *data; size_t len, cap; } SB;

/* Append n bytes of s, growing as needed. */
static void sb_putn(SB *sb, const char *s, size_t n) {
    if (sb->len + n + 1 > sb->cap) {
        size_t cap = sb->cap ? sb->cap : 1024;
        while (sb->len + n + 1 > cap) cap *= 2;
        char *p = realloc(sb->data, cap);
        if (!p) return;
        sb->data = p; sb->cap = cap;
    }
    memcpy(sb->data + sb->len, s, n);
    sb->len += n; sb->data[sb->len] = '\0';
}

static void sb_puts(SB *sb, const char *s) { sb_putn(sb, s, strlen(s)); }

/* Append text escaped for HTML element content: & < > become entities. */
static void sb_esc_text(SB *sb, const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        switch (s[i]) {
            case '&': sb_puts(sb, "&amp;"); break;
            case '<': sb_puts(sb, "&lt;");  break;
            case '>': sb_puts(sb, "&gt;");  break;
            default:  sb_putn(sb, &s[i], 1);
        }
    }
}

/* Append text escaped for a double-quoted HTML attribute (also escapes "). */
static void sb_esc_attr(SB *sb, const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        switch (s[i]) {
            case '&':  sb_puts(sb, "&amp;");  break;
            case '<':  sb_puts(sb, "&lt;");   break;
            case '>':  sb_puts(sb, "&gt;");   break;
            case '"':  sb_puts(sb, "&quot;"); break;
            default:   sb_putn(sb, &s[i], 1);
        }
    }
}

#define TIGHT_MAX 64

/* Renderer state threaded through the md4c callbacks. */
typedef struct {
    SB    out;             /* the HTML <body> contents */
    int   in_code_block;   /* inside MD_BLOCK_CODE (accumulate, then highlight) */
    int   code_lang;       /* hl_lang() of the current code block, or -1 */
    SB    code;            /* accumulated raw code text of the current block */
    int   img_depth;       /* inside an <img> alt text (emit plain, no tags) */
    int   tight[TIGHT_MAX];/* per-list tightness, innermost on top */
    int   tdepth;          /* list-nesting depth into `tight` */
    int   p_suppress;      /* the current MD_BLOCK_P sits in a tight list item */
} Ctx;

/* Append an md4c attribute (href/src/target …) escaped for an HTML attribute. */
static void put_attr(SB *sb, const MD_ATTRIBUTE *a) {
    if (a->text && a->size) sb_esc_attr(sb, a->text, a->size);
}

/* One highlighted span of code -> a coloured <span> (or bare escaped text for
 * the default class). Class letters map to CSS rules emitted in the <style>. */
static void hl_emit(void *ud, HLKind kind, const char *s, size_t len) {
    SB *out = ud;
    const char *cls = NULL;
    switch (kind) {
        case HL_KEYWORD:  cls = "k"; break;
        case HL_TYPE:     cls = "t"; break;
        case HL_STRING:   cls = "s"; break;
        case HL_NUMBER:   cls = "n"; break;
        case HL_COMMENT:  cls = "c"; break;
        case HL_FUNCTION: cls = "f"; break;
        case HL_VARIABLE: cls = "v"; break;
        case HL_PROPERTY: cls = "p"; break;
        case HL_OPERATOR: cls = "o"; break;
        case HL_TEXT:     default: break;
    }
    if (cls) { sb_puts(out, "<span class=\"hl-"); sb_puts(out, cls); sb_puts(out, "\">"); }
    sb_esc_text(out, s, len);
    if (cls) sb_puts(out, "</span>");
}

/* Flush the accumulated code block as <pre><code>, syntax-highlighted line by
 * line when the language is known, else escaped verbatim. */
static void flush_code(Ctx *c) {
    SB *o = &c->out;
    sb_puts(o, "<pre><code>");
    const char *s = c->code.data ? c->code.data : "";
    size_t n = c->code.len;
    if (n && s[n-1] == '\n') n--;          /* code blocks carry a trailing '\n' */

    if (c->code_lang >= 0) {
        HLState st; memset(&st, 0, sizeof st);
        size_t i = 0;
        while (i <= n) {
            size_t j = i;
            while (j < n && s[j] != '\n') j++;
            hl_line(c->code_lang, &st, s + i, j - i, hl_emit, o);
            if (j < n) sb_putn(o, "\n", 1);
            i = j + 1;
        }
    } else {
        sb_esc_text(o, s, n);
    }
    sb_puts(o, "</code></pre>\n");
    c->code.len = 0;
    if (c->code.data) c->code.data[0] = '\0';
}

/* text-align style for a table cell, or "" for the default alignment. */
static const char *align_style(MD_ALIGN a) {
    switch (a) {
        case MD_ALIGN_LEFT:   return " style=\"text-align:left\"";
        case MD_ALIGN_CENTER: return " style=\"text-align:center\"";
        case MD_ALIGN_RIGHT:  return " style=\"text-align:right\"";
        default:              return "";
    }
}

static int cb_enter_block(MD_BLOCKTYPE type, void *detail, void *ud) {
    Ctx *c = ud;
    SB *o = &c->out;
    switch (type) {
        case MD_BLOCK_DOC: break;
        case MD_BLOCK_QUOTE: sb_puts(o, "<blockquote>\n"); break;
        case MD_BLOCK_UL: {
            MD_BLOCK_UL_DETAIL *d = detail;
            if (c->tdepth < TIGHT_MAX) c->tight[c->tdepth] = d->is_tight;
            c->tdepth++;
            sb_puts(o, "<ul>\n");
            break;
        }
        case MD_BLOCK_OL: {
            MD_BLOCK_OL_DETAIL *d = detail;
            if (c->tdepth < TIGHT_MAX) c->tight[c->tdepth] = d->is_tight;
            c->tdepth++;
            if (d->start != 1) { char b[32]; snprintf(b, sizeof b, "<ol start=\"%u\">\n", d->start); sb_puts(o, b); }
            else sb_puts(o, "<ol>\n");
            break;
        }
        case MD_BLOCK_LI: {
            MD_BLOCK_LI_DETAIL *d = detail;
            if (d->is_task) {
                sb_puts(o, "<li class=\"task\"><input type=\"checkbox\" disabled");
                if (d->task_mark == 'x' || d->task_mark == 'X') sb_puts(o, " checked");
                sb_puts(o, "> ");
            } else {
                sb_puts(o, "<li>");
            }
            break;
        }
        case MD_BLOCK_HR: sb_puts(o, "<hr>\n"); break;
        case MD_BLOCK_H: {
            MD_BLOCK_H_DETAIL *d = detail;
            char b[8]; snprintf(b, sizeof b, "<h%u>", d->level); sb_puts(o, b);
            break;
        }
        case MD_BLOCK_CODE: {
            MD_BLOCK_CODE_DETAIL *d = detail;
            c->in_code_block = 1;
            c->code.len = 0;
            c->code_lang = (d->lang.text && d->lang.size)
                         ? hl_lang(d->lang.text, d->lang.size) : -1;
            break;
        }
        case MD_BLOCK_HTML: break;     /* raw HTML arrives via text() verbatim */
        case MD_BLOCK_P:
            c->p_suppress = (c->tdepth > 0 && c->tdepth <= TIGHT_MAX &&
                             c->tight[c->tdepth - 1]);
            if (!c->p_suppress) sb_puts(o, "<p>");
            break;
        case MD_BLOCK_TABLE: sb_puts(o, "<table>\n"); break;
        case MD_BLOCK_THEAD: sb_puts(o, "<thead>\n"); break;
        case MD_BLOCK_TBODY: sb_puts(o, "<tbody>\n"); break;
        case MD_BLOCK_TR:    sb_puts(o, "<tr>"); break;
        case MD_BLOCK_TH: { MD_BLOCK_TD_DETAIL *d = detail; sb_puts(o, "<th"); sb_puts(o, align_style(d->align)); sb_puts(o, ">"); break; }
        case MD_BLOCK_TD: { MD_BLOCK_TD_DETAIL *d = detail; sb_puts(o, "<td"); sb_puts(o, align_style(d->align)); sb_puts(o, ">"); break; }
    }
    return 0;
}

static int cb_leave_block(MD_BLOCKTYPE type, void *detail, void *ud) {
    (void)detail;
    Ctx *c = ud;
    SB *o = &c->out;
    switch (type) {
        case MD_BLOCK_DOC: break;
        case MD_BLOCK_QUOTE: sb_puts(o, "</blockquote>\n"); break;
        case MD_BLOCK_UL: if (c->tdepth > 0) c->tdepth--; sb_puts(o, "</ul>\n"); break;
        case MD_BLOCK_OL: if (c->tdepth > 0) c->tdepth--; sb_puts(o, "</ol>\n"); break;
        case MD_BLOCK_LI: sb_puts(o, "</li>\n"); break;
        case MD_BLOCK_HR: break;
        case MD_BLOCK_H: { MD_BLOCK_H_DETAIL *d = detail; char b[8]; snprintf(b, sizeof b, "</h%u>\n", d->level); sb_puts(o, b); break; }
        case MD_BLOCK_CODE: flush_code(c); c->in_code_block = 0; break;
        case MD_BLOCK_HTML: break;
        case MD_BLOCK_P: if (!c->p_suppress) sb_puts(o, "</p>\n"); c->p_suppress = 0; break;
        case MD_BLOCK_TABLE: sb_puts(o, "</table>\n"); break;
        case MD_BLOCK_THEAD: sb_puts(o, "</thead>\n"); break;
        case MD_BLOCK_TBODY: sb_puts(o, "</tbody>\n"); break;
        case MD_BLOCK_TR:    sb_puts(o, "</tr>\n"); break;
        case MD_BLOCK_TH:    sb_puts(o, "</th>"); break;
        case MD_BLOCK_TD:    sb_puts(o, "</td>"); break;
    }
    return 0;
}

static int cb_enter_span(MD_SPANTYPE type, void *detail, void *ud) {
    Ctx *c = ud;
    SB *o = &c->out;
    if (c->img_depth) {                    /* inside alt text: keep nesting count */
        if (type == MD_SPAN_IMG) c->img_depth++;
        return 0;                          /* emit no tags, just the alt text */
    }
    switch (type) {
        case MD_SPAN_EM:     sb_puts(o, "<em>"); break;
        case MD_SPAN_STRONG: sb_puts(o, "<strong>"); break;
        case MD_SPAN_DEL:    sb_puts(o, "<del>"); break;
        case MD_SPAN_U:      sb_puts(o, "<u>"); break;
        case MD_SPAN_CODE:   sb_puts(o, "<code>"); break;
        case MD_SPAN_A: {
            MD_SPAN_A_DETAIL *d = detail;
            sb_puts(o, "<a href=\""); put_attr(o, &d->href); sb_puts(o, "\">");
            break;
        }
        case MD_SPAN_WIKILINK: {
            MD_SPAN_WIKILINK_DETAIL *d = detail;
            sb_puts(o, "<a class=\"wikilink\" href=\""); put_attr(o, &d->target); sb_puts(o, "\">");
            break;
        }
        case MD_SPAN_IMG: {
            MD_SPAN_IMG_DETAIL *d = detail;
            sb_puts(o, "<img src=\""); put_attr(o, &d->src); sb_puts(o, "\" alt=\"");
            c->img_depth = 1;              /* the nested text becomes the alt= value */
            break;
        }
        default: break;                    /* LaTeX math: rendered as its text */
    }
    return 0;
}

static int cb_leave_span(MD_SPANTYPE type, void *detail, void *ud) {
    (void)detail;
    Ctx *c = ud;
    SB *o = &c->out;
    if (c->img_depth) {
        if (type == MD_SPAN_IMG && --c->img_depth == 0) sb_puts(o, "\">");
        return 0;
    }
    switch (type) {
        case MD_SPAN_EM:       sb_puts(o, "</em>"); break;
        case MD_SPAN_STRONG:   sb_puts(o, "</strong>"); break;
        case MD_SPAN_DEL:      sb_puts(o, "</del>"); break;
        case MD_SPAN_U:        sb_puts(o, "</u>"); break;
        case MD_SPAN_CODE:     sb_puts(o, "</code>"); break;
        case MD_SPAN_A:        sb_puts(o, "</a>"); break;
        case MD_SPAN_WIKILINK: sb_puts(o, "</a>"); break;
        default: break;
    }
    return 0;
}

static int cb_text(MD_TEXTTYPE type, const MD_CHAR *text, MD_SIZE size, void *ud) {
    Ctx *c = ud;
    SB *o = &c->out;
    if (c->in_code_block) { sb_putn(&c->code, text, size); return 0; }
    /* In an <img> alt, everything is plain attribute text. */
    if (c->img_depth) {
        if (type == MD_TEXT_BR || type == MD_TEXT_SOFTBR) sb_puts(o, " ");
        else sb_esc_attr(o, text, size);
        return 0;
    }
    switch (type) {
        case MD_TEXT_NORMAL: sb_esc_text(o, text, size); break;
        case MD_TEXT_NULLCHAR: sb_puts(o, "\xEF\xBF\xBD"); break;  /* U+FFFD */
        case MD_TEXT_BR:     sb_puts(o, "<br>\n"); break;
        case MD_TEXT_SOFTBR: sb_puts(o, "\n"); break;
        case MD_TEXT_ENTITY: sb_putn(o, text, size); break;        /* already an entity */
        case MD_TEXT_HTML:   sb_putn(o, text, size); break;        /* raw HTML, verbatim */
        case MD_TEXT_CODE:   sb_esc_text(o, text, size); break;    /* inline `code` span */
        case MD_TEXT_LATEXMATH: sb_esc_text(o, text, size); break;
    }
    return 0;
}

/* Format an RGB as "#rrggbb" into a 8-byte buffer (caller-owned). */
static void hexcolor(char out[8], RGB c) {
    snprintf(out, 8, "#%02x%02x%02x", c.r, c.g, c.b);
}

/* Emit the embedded <style> derived from the theme palette. */
static void emit_style(SB *o, const Theme *t) {
    char fg[8], bg[8], hx[8];
    /* Page colours follow the theme's polarity; accents come from the palette. */
    if (t->dark) { strcpy(bg, "#1b1b1f"); strcpy(fg, "#d7d7da"); }
    else         { strcpy(bg, "#ffffff"); strcpy(fg, "#24292f"); }

    sb_puts(o, "<style>\n");
    char buf[512];
    snprintf(buf, sizeof buf,
        "body{background:%s;color:%s;margin:0;padding:2rem 1rem;"
        "font:16px/1.6 -apple-system,Segoe UI,Roboto,Helvetica,Arial,sans-serif;}\n"
        "main{max-width:46rem;margin:0 auto;}\n", bg, fg);
    sb_puts(o, buf);

    for (int i = 0; i < 6; i++) {
        hexcolor(hx, t->heading[i]);
        snprintf(buf, sizeof buf, "h%d{color:%s;line-height:1.25;}\n", i + 1, hx);
        sb_puts(o, buf);
    }
    hexcolor(hx, t->link);
    snprintf(buf, sizeof buf, "a{color:%s;text-decoration:none;}a:hover{text-decoration:underline;}\n", hx);
    sb_puts(o, buf);
    snprintf(buf, sizeof buf, "a.wikilink{border-bottom:1px dotted %s;}\n", hx);
    sb_puts(o, buf);

    char cbg[8], cfg[8];
    hexcolor(cbg, t->code_bg); hexcolor(cfg, t->code_fg);
    snprintf(buf, sizeof buf,
        "code{background:%s;color:%s;border-radius:4px;padding:.1em .3em;"
        "font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;font-size:.9em;}\n"
        "pre{background:%s;border-radius:8px;padding:1rem;overflow:auto;}\n"
        "pre code{background:none;padding:0;font-size:.85em;line-height:1.5;}\n", cbg, cfg, cbg);
    sb_puts(o, buf);

    hexcolor(hx, t->quote);
    snprintf(buf, sizeof buf,
        "blockquote{border-left:3px solid %s;margin:1rem 0;padding:.2rem 1rem;color:%s;opacity:.9;}\n", hx, hx);
    sb_puts(o, buf);
    hexcolor(hx, t->rule);
    snprintf(buf, sizeof buf, "hr{border:none;border-top:1px solid %s;margin:1.5rem 0;}\n", hx);
    sb_puts(o, buf);

    snprintf(buf, sizeof buf,
        "table{border-collapse:collapse;margin:1rem 0;}\n"
        "th,td{border:1px solid %s;padding:.4rem .7rem;}\n"
        "th{background:%s;}\n"
        "img{max-width:100%%;height:auto;}\n"
        "li.task{list-style:none;}li.task input{margin-right:.4em;}\n", cbg, cbg);
    sb_puts(o, buf);

    /* Syntax-highlight classes: indexed by HLKind via the theme's syntax[]. */
    static const struct { const char *cls; HLKind k; } SY[] = {
        {"k", HL_KEYWORD}, {"t", HL_TYPE}, {"s", HL_STRING}, {"n", HL_NUMBER},
        {"c", HL_COMMENT}, {"f", HL_FUNCTION}, {"v", HL_VARIABLE},
        {"p", HL_PROPERTY}, {"o", HL_OPERATOR},
    };
    for (size_t i = 0; i < sizeof SY / sizeof SY[0]; i++) {
        hexcolor(hx, t->syntax[SY[i].k]);
        snprintf(buf, sizeof buf, ".hl-%s{color:%s;}\n", SY[i].cls, hx);
        sb_puts(o, buf);
    }
    sb_puts(o, "</style>\n");
}

char *md_to_html(const char *src, size_t len, const Theme *theme, const char *title) {
    Ctx c; memset(&c, 0, sizeof c);
    c.code_lang = -1;

    MD_PARSER parser;
    memset(&parser, 0, sizeof parser);
    parser.abi_version = 0;
    parser.flags = MD_DIALECT_GITHUB | MD_FLAG_PERMISSIVEATXHEADERS | MD_FLAG_WIKILINKS;
    parser.enter_block = cb_enter_block;
    parser.leave_block = cb_leave_block;
    parser.enter_span  = cb_enter_span;
    parser.leave_span  = cb_leave_span;
    parser.text        = cb_text;

    if (md_parse(src, (MD_SIZE)len, &parser, &c) != 0) {
        free(c.out.data); free(c.code.data);
        return NULL;
    }

    /* Assemble the full document around the rendered body. */
    SB page; memset(&page, 0, sizeof page);
    sb_puts(&page, "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
                   "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n<title>");
    sb_esc_text(&page, title ? title : "glance", strlen(title ? title : "glance"));
    sb_puts(&page, "</title>\n");
    emit_style(&page, theme);
    sb_puts(&page, "</head>\n<body>\n<main>\n");
    if (c.out.data) sb_putn(&page, c.out.data, c.out.len);
    sb_puts(&page, "</main>\n</body>\n</html>\n");

    free(c.out.data); free(c.code.data);
    if (!page.data) page.data = calloc(1, 1);
    return page.data;
}
