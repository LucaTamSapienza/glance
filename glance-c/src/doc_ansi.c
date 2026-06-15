/* doc_ansi.c — serialize a Doc to an ANSI string (for the render-only CLI and
 * for tests). The TUI does not use this path; it blits runs to notcurses cells
 * directly. Keeping the serializer separate means the model has two
 * independent sinks and neither one is privileged.
 */
#include "render.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* A growable, NUL-terminated string builder. */
typedef struct { char *data; size_t len, cap; } SB;

/* Append n bytes of s to the builder, growing as needed. */
static void sb_putn(SB *sb, const char *s, size_t n) {
    if (sb->len + n + 1 > sb->cap) {
        size_t cap = sb->cap ? sb->cap : 256;
        while (sb->len + n + 1 > cap) cap *= 2;
        char *p = realloc(sb->data, cap);
        if (!p) return;
        sb->data = p; sb->cap = cap;
    }
    memcpy(sb->data + sb->len, s, n);
    sb->len += n; sb->data[sb->len] = '\0';
}

/* Append a NUL-terminated string. */
static void sb_puts(SB *sb, const char *s) { sb_putn(sb, s, strlen(s)); }

/* Emit the SGR escape for a Style (reset + flags + truecolor fg/bg). */
static void emit_sgr(SB *sb, const Style *st) {
    char buf[96];
    int p = snprintf(buf, sizeof buf, "\033[0");
    if (st->bold)      p += snprintf(buf + p, sizeof buf - p, ";1");
    if (st->dim)       p += snprintf(buf + p, sizeof buf - p, ";2");
    if (st->italic)    p += snprintf(buf + p, sizeof buf - p, ";3");
    if (st->underline) p += snprintf(buf + p, sizeof buf - p, ";4");
    if (st->strike)    p += snprintf(buf + p, sizeof buf - p, ";9");
    if (st->has_fg)
        p += snprintf(buf + p, sizeof buf - p, ";38;2;%u;%u;%u",
                      st->fg.r, st->fg.g, st->fg.b);
    if (st->has_bg)
        p += snprintf(buf + p, sizeof buf - p, ";48;2;%u;%u;%u",
                      st->bg.r, st->bg.g, st->bg.b);
    snprintf(buf + p, sizeof buf - p, "m");
    sb_puts(sb, buf);
}

/* Serialize a Doc to an ANSI string: per run, emit its SGR then text; pad
 * code-block lines with their background to the document width. See render.h. */
char *doc_to_ansi(const Doc *d) {
    SB sb; memset(&sb, 0, sizeof sb);
    for (size_t i = 0; i < d->nline; i++) {
        const Line *L = &d->lines[i];
        for (size_t j = 0; j < L->nrun; j++) {
            emit_sgr(&sb, &L->runs[j].st);
            sb_putn(&sb, L->runs[j].text, L->runs[j].len);
        }
        if (L->fill && L->cols < d->width) {
            char bg[48];
            snprintf(bg, sizeof bg, "\033[0;48;2;%u;%u;%um",
                     L->fill_bg.r, L->fill_bg.g, L->fill_bg.b);
            sb_puts(&sb, bg);
            for (int k = L->cols; k < d->width; k++) sb_puts(&sb, " ");
        }
        sb_puts(&sb, "\033[0m\n");
    }
    if (!sb.data) sb.data = calloc(1, 1);
    return sb.data;
}
