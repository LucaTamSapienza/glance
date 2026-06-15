/* preprocess.c — tolerant-Markdown source fix-ups (see preprocess.h).
 *
 * Works line by line. Bold tightening rewrites "** x **" to "**x**" outside of
 * code; setext neutralizing inserts a blank line before a stray "---"/"==="
 * underline so it can't absorb the paragraph above into a heading. Fenced and
 * inline code are left untouched.
 */
#include "preprocess.h"

#include <stdlib.h>
#include <string.h>

/* ---- tiny string builder -------------------------------------------------- */

typedef struct { char *data; size_t len, cap; } SB;

/* Append n bytes, growing the buffer as needed. */
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
static void sb_putc(SB *sb, char c) { sb_putn(sb, &c, 1); }

/* ---- line classification -------------------------------------------------- */

/* Offsets of the first and one-past-last non-space/tab byte of [s, s+n). */
static void trim_bounds(const char *s, size_t n, size_t *lo, size_t *hi) {
    size_t a = 0, b = n;
    while (a < b && (s[a] == ' ' || s[a] == '\t')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) b--;
    *lo = a; *hi = b;
}

/* A fence marker line: trimmed text starts with ``` or ~~~. */
static int is_fence(const char *s, size_t n) {
    size_t lo, hi; trim_bounds(s, n, &lo, &hi);
    return hi - lo >= 3 &&
           ((s[lo] == '`' && s[lo+1] == '`' && s[lo+2] == '`') ||
            (s[lo] == '~' && s[lo+1] == '~' && s[lo+2] == '~'));
}

/* A setext underline: a run of only '-' or only '=' (length >= 1). */
static int is_setext_underline(const char *s, size_t n) {
    size_t lo, hi; trim_bounds(s, n, &lo, &hi);
    if (hi == lo) return 0;
    char c = s[lo];
    if (c != '-' && c != '=') return 0;
    for (size_t i = lo; i < hi; i++) if (s[i] != c) return 0;
    return 1;
}

/* A thematic break: >= 3 of one of - * _, with optional spaces between. */
static int is_thematic_break(const char *s, size_t n) {
    size_t lo, hi; trim_bounds(s, n, &lo, &hi);
    if (hi - lo < 3) return 0;
    char c = s[lo];
    if (c != '-' && c != '*' && c != '_') return 0;
    int count = 0;
    for (size_t i = lo; i < hi; i++) {
        if (s[i] == c) count++;
        else if (s[i] != ' ' && s[i] != '\t') return 0;
    }
    return count >= 3;
}

/* ---- bold tightening ------------------------------------------------------ */

/* Tighten one delimiter (d = '*' or '_', doubled) within a code-free segment:
 * "<d><d> x <d><d>" with no inner d becomes "<d><d>x<d><d>". Empty spans
 * ("** **") and already-tight spans are left as they are. */
static void tighten_segment(SB *out, const char *s, size_t n, char d) {
    size_t i = 0;
    while (i < n) {
        if (i + 1 < n && s[i] == d && s[i + 1] == d) {
            size_t k = i + 2;                 /* find the next d */
            while (k < n && s[k] != d) k++;
            if (k + 1 < n && s[k] == d && s[k + 1] == d) {
                size_t lo, hi;                /* inner = [i+2, k) */
                trim_bounds(s + i + 2, k - (i + 2), &lo, &hi);
                lo += i + 2; hi += i + 2;
                if (hi > lo && (lo != i + 2 || hi != k)) {
                    sb_putc(out, d); sb_putc(out, d);
                    sb_putn(out, s + lo, hi - lo);
                    sb_putc(out, d); sb_putc(out, d);
                } else {
                    sb_putn(out, s + i, k + 2 - i);   /* empty or already tight */
                }
                i = k + 2;
                continue;
            }
            sb_putn(out, s + i, 2);           /* unmatched: emit "<d><d>" */
            i += 2;
            continue;
        }
        sb_putc(out, s[i++]);
    }
}

/* Apply bold tightening to a line, copying inline code spans (`...`) verbatim. */
static void tighten_line(SB *out, const char *s, size_t n) {
    size_t i = 0;
    while (i < n) {
        if (s[i] == '`') {
            size_t j = i; while (j < n && s[j] == '`') j++;
            size_t run = j - i;
            size_t k = j, close = n;          /* find a backtick run of equal length */
            while (k < n) {
                if (s[k] == '`') {
                    size_t m = k; while (m < n && s[m] == '`') m++;
                    if (m - k == run) { close = k; break; }
                    k = m;
                } else k++;
            }
            if (close < n) { sb_putn(out, s + i, close + run - i); i = close + run; continue; }
            sb_putn(out, s + i, run); i = j; continue;   /* unterminated run */
        }
        size_t start = i;
        while (i < n && s[i] != '`') i++;
        /* tighten ** then __ over this code-free segment, via a scratch buffer */
        SB tmp = {0};
        tighten_segment(&tmp, s + start, i - start, '*');
        SB tmp2 = {0};
        tighten_segment(&tmp2, tmp.data ? tmp.data : "", tmp.len, '_');
        sb_putn(out, tmp2.data ? tmp2.data : "", tmp2.len);
        free(tmp.data); free(tmp2.data);
    }
}

/* ---- driver --------------------------------------------------------------- */

char *preprocess(const char *src, size_t len, size_t *out_len) {
    SB out = {0};
    int in_fence = 0, prev_nonblank = 0;
    size_t i = 0;
    while (i <= len) {
        size_t start = i;
        while (i < len && src[i] != '\n') i++;
        size_t llen = i - start;             /* current line, sans newline */
        const char *line = src + start;

        int fence = is_fence(line, llen);
        if (!in_fence && !fence && prev_nonblank &&
            (is_thematic_break(line, llen) || is_setext_underline(line, llen)))
            sb_putc(&out, '\n');             /* keep it off the paragraph above */

        if (fence) {
            in_fence = !in_fence;
            sb_putn(&out, line, llen);
        } else if (in_fence) {
            sb_putn(&out, line, llen);       /* code content: verbatim */
        } else {
            tighten_line(&out, line, llen);
        }

        size_t lo, hi; trim_bounds(line, llen, &lo, &hi);
        prev_nonblank = hi > lo;

        if (i < len) sb_putc(&out, '\n');    /* re-add the separator */
        i++;                                  /* step past the '\n' (or end) */
    }
    if (!out.data) out.data = calloc(1, 1);
    if (out_len) *out_len = out.len;
    return out.data;
}
