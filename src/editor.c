/* editor.c — line-array text buffer with a rune-aware cursor.
 *
 * Pure model: split source into lines, edit them, serialize back. No terminal
 * dependency, which makes it unit-testable on its own. Display width comes from
 * util.h's u8_* helpers, so the editor and renderer agree on column counting.
 */
#include "editor.h"
#include "util.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* ---- column <-> byte conversion ------------------------------------------ */

/* Display column of the byte offset `byte` within line b. */
int ed_byte_to_col(const char *b, size_t byte) {
    return u8_width(b, byte);
}

/* Byte offset of display column `col` within the `len`-byte line b. */
size_t ed_col_to_byte(const char *b, size_t len, int col) {
    size_t i = 0;
    for (int c = 0; i < len && c < col; c++)
        i += u8_runelen((unsigned char)b[i]);
    return i > len ? len : i;
}

/* ---- line primitives ------------------------------------------------------ */

/* Ensure line L can hold `need` more bytes (plus a trailing NUL). */
static void el_reserve(ELine *L, size_t need) {
    if (L->len + need + 1 <= L->cap) return;
    size_t cap = L->cap ? L->cap : 16;
    while (L->len + need + 1 > cap) cap *= 2;
    char *p = realloc(L->b, cap);
    if (!p) return;
    L->b = p; L->cap = cap;
}

/* Replace L's contents with the n bytes at s. */
static void el_set(ELine *L, const char *s, size_t n) {
    el_reserve(L, n);
    if (!L->b) return;
    memcpy(L->b, s, n);
    L->len = n; L->b[n] = '\0';
}

/* Insert n bytes from s at byte offset pos within L. */
static void el_insert(ELine *L, size_t pos, const char *s, size_t n) {
    el_reserve(L, n);
    if (!L->b) return;
    memmove(L->b + pos + n, L->b + pos, L->len - pos);
    memcpy(L->b + pos, s, n);
    L->len += n; L->b[L->len] = '\0';
}

/* Remove n bytes at byte offset pos within L (clamped to the line end). */
static void el_erase(ELine *L, size_t pos, size_t n) {
    if (pos + n > L->len) n = L->len - pos;
    memmove(L->b + pos, L->b + pos + n, L->len - pos - n);
    L->len -= n;
    if (L->b) L->b[L->len] = '\0';
}

/* ---- line-array primitives ------------------------------------------------ */

/* Ensure the line array can hold `need` more lines. */
static void lines_reserve(Editor *e, size_t need) {
    if (e->n + need <= e->cap) return;
    size_t cap = e->cap ? e->cap : 32;
    while (e->n + need > cap) cap *= 2;
    ELine *p = realloc(e->lines, cap * sizeof(ELine));
    if (!p) return;
    e->lines = p; e->cap = cap;
}

/* Insert a fresh empty line at index idx and return it. */
static ELine *lines_insert(Editor *e, size_t idx) {
    lines_reserve(e, 1);
    memmove(&e->lines[idx + 1], &e->lines[idx], (e->n - idx) * sizeof(ELine));
    e->lines[idx].b = NULL; e->lines[idx].len = 0; e->lines[idx].cap = 0;
    e->n++;
    return &e->lines[idx];
}

/* Free and remove the line at idx, closing the gap. */
static void lines_remove(Editor *e, size_t idx) {
    free(e->lines[idx].b);
    memmove(&e->lines[idx], &e->lines[idx + 1], (e->n - idx - 1) * sizeof(ELine));
    e->n--;
}

/* ---- init / teardown / serialize ----------------------------------------- */

/* Split src into lines (on '\n'); the cursor starts at the top-left. */
void editor_init(Editor *e, const char *src, size_t len) {
    memset(e, 0, sizeof *e);
    e->goal_col = -1;
    size_t start = 0;
    for (size_t i = 0; i <= len; i++) {
        if (i == len || src[i] == '\n') {
            ELine *L = lines_insert(e, e->n);
            el_set(L, src + start, i - start);
            start = i + 1;
            if (i == len) break;
        }
    }
    if (e->n == 0) lines_insert(e, 0);   /* always at least one line */
}

/* Release the buffer and reset it to an empty state. */
void editor_free(Editor *e) {
    for (size_t i = 0; i < e->n; i++) free(e->lines[i].b);
    free(e->lines);
    memset(e, 0, sizeof *e);
}

/* Join the lines with '\n' (no trailing newline) into an owned string. */
char *editor_source(const Editor *e, size_t *out_len) {
    size_t total = 0;
    for (size_t i = 0; i < e->n; i++) total += e->lines[i].len + 1; /* + '\n' */
    char *out = malloc(total + 1);
    if (!out) { if (out_len) *out_len = 0; return NULL; }
    size_t p = 0;
    for (size_t i = 0; i < e->n; i++) {
        memcpy(out + p, e->lines[i].b ? e->lines[i].b : "", e->lines[i].len);
        p += e->lines[i].len;
        if (i + 1 < e->n) out[p++] = '\n';   /* no trailing newline */
    }
    out[p] = '\0';
    if (out_len) *out_len = p;
    return out;
}

/* ---- editing -------------------------------------------------------------- */

/* The line the cursor is on. */
static ELine *cur(Editor *e) { return &e->lines[e->cy]; }

/* Insert n bytes (no '\n') at the cursor and advance past them. */
void editor_insert(Editor *e, const char *s, size_t n) {
    el_insert(cur(e), e->cx, s, n);
    e->cx += n;
    e->dirty = 1;
    e->goal_col = -1;
}

/* Split the current line at the cursor, moving the tail to a new line below. */
void editor_newline(Editor *e) {
    ELine *L = cur(e);
    size_t tail_len = L->len - e->cx;
    char *tail = L->b ? L->b + e->cx : "";        /* avoid memcpy(NULL,0) UB on an empty line */
    ELine *nl = lines_insert(e, e->cy + 1);
    el_set(nl, tail, tail_len);
    /* truncate current line at the cursor (note lines_insert may realloc) */
    L = &e->lines[e->cy];
    L->len = e->cx;
    if (L->b) L->b[L->len] = '\0';
    e->cy++; e->cx = 0;
    e->dirty = 1;
    e->goal_col = -1;
}

/* Delete the rune before the cursor, or join with the previous line at col 0. */
void editor_backspace(Editor *e) {
    if (e->cx > 0) {
        ELine *L = cur(e);
        size_t prev = e->cx - 1;
        while (prev > 0 && u8_cont((unsigned char)L->b[prev])) prev--;
        el_erase(L, prev, e->cx - prev);
        e->cx = prev;
    } else if (e->cy > 0) {
        ELine *prevL = &e->lines[e->cy - 1];
        size_t join = prevL->len;
        ELine *L = cur(e);
        el_insert(prevL, prevL->len, L->b ? L->b : "", L->len);
        lines_remove(e, e->cy);
        e->cy--; e->cx = (int)join;
    } else {
        return;
    }
    e->dirty = 1;
    e->goal_col = -1;
}

/* Delete the rune at the cursor, or pull up the next line at end of line. */
void editor_delete(Editor *e) {
    ELine *L = cur(e);
    if ((size_t)e->cx < L->len) {
        size_t next = e->cx + 1;
        while (next < L->len && u8_cont((unsigned char)L->b[next])) next++;
        el_erase(L, e->cx, next - e->cx);
    } else if ((size_t)e->cy + 1 < e->n) {
        ELine *nl = &e->lines[e->cy + 1];
        el_insert(L, L->len, nl->b ? nl->b : "", nl->len);
        lines_remove(e, e->cy + 1);
    } else {
        return;
    }
    e->dirty = 1;
    e->goal_col = -1;
}

/* ---- movement ------------------------------------------------------------- */

/* Display column of the cursor within its line. */
int editor_cursor_col(const Editor *e) {
    return ed_byte_to_col(e->lines[e->cy].b ? e->lines[e->cy].b : "", e->cx);
}

/* Move one rune left, wrapping to the end of the previous line. */
void editor_left(Editor *e) {
    if (e->cx > 0) {
        ELine *L = cur(e);
        size_t p = e->cx - 1;
        while (p > 0 && u8_cont((unsigned char)L->b[p])) p--;
        e->cx = (int)p;
    } else if (e->cy > 0) {
        e->cy--;
        e->cx = (int)e->lines[e->cy].len;
    }
    e->goal_col = -1;
}

/* Move one rune right, wrapping to the start of the next line. */
void editor_right(Editor *e) {
    ELine *L = cur(e);
    if ((size_t)e->cx < L->len) {
        e->cx += u8_runelen((unsigned char)L->b[e->cx]);
        if ((size_t)e->cx > L->len) e->cx = (int)L->len;
    } else if ((size_t)e->cy + 1 < e->n) {
        e->cy++; e->cx = 0;
    }
    e->goal_col = -1;
}

/* move to a target display column on the current line, clamping to its end */
static void seek_goal(Editor *e) {
    ELine *L = cur(e);
    e->cx = (int)ed_col_to_byte(L->b ? L->b : "", L->len, e->goal_col);
}

/* Move up one line, keeping the goal column (set on the first vertical move). */
void editor_up(Editor *e) {
    if (e->goal_col < 0) e->goal_col = editor_cursor_col(e);
    if (e->cy > 0) { e->cy--; seek_goal(e); }
}

/* Move down one line, keeping the goal column. */
void editor_down(Editor *e) {
    if (e->goal_col < 0) e->goal_col = editor_cursor_col(e);
    if ((size_t)e->cy + 1 < e->n) { e->cy++; seek_goal(e); }
}

/* Jump to the start / end of the current line. */
void editor_home(Editor *e) { e->cx = 0; e->goal_col = -1; }
void editor_end(Editor *e)  { e->cx = (int)cur(e)->len; e->goal_col = -1; }

/* A word separator: ASCII space or tab. Multibyte runes are continuation/lead
 * bytes (>= 0x80), never separators, so word jumps stop on real boundaries and
 * the cursor stays on a rune boundary. */
/* True for bytes that separate words: whitespace and ASCII punctuation. A word
 * is a run of alphanumerics, '_', or non-ASCII (UTF-8) bytes, so accented
 * letters stay inside a word while punctuation stops the jump. */
static int ed_wordsep(unsigned char b) {
    if (b >= 0x80) return 0;
    return !(isalnum(b) || b == '_');
}

/* Move to the start of the previous word (skip separators, then the word);
 * wraps to the end of the previous line when already at column 0. */
void editor_word_left(Editor *e) {
    if (e->cx == 0) { editor_left(e); return; }
    ELine *L = cur(e);
    int p = e->cx;
    while (p > 0 && ed_wordsep((unsigned char)L->b[p - 1])) p--;
    while (p > 0 && !ed_wordsep((unsigned char)L->b[p - 1])) p--;
    e->cx = p;
    e->goal_col = -1;
}

/* Move to the end of the current or next word (skip separators, then the
 * word) — the macOS/emacs Option-arrow convention; wraps to the start of the
 * next line when already at the end. */
void editor_word_right(Editor *e) {
    ELine *L = cur(e);
    if ((size_t)e->cx >= L->len) { editor_right(e); return; }
    int p = e->cx, n = (int)L->len;
    while (p < n && ed_wordsep((unsigned char)L->b[p])) p++;
    while (p < n && !ed_wordsep((unsigned char)L->b[p])) p++;
    e->cx = p;
    e->goal_col = -1;
}
