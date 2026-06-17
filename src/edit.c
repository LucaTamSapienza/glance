/* edit.c — surgical edits on raw Markdown source (see edit.h). */
#include "edit.h"
#include "section.h"   /* section_title_matches */

#include <stdlib.h>
#include <string.h>

/* ---- a growable byte buffer ----------------------------------------------- */

typedef struct { char *p; size_t len, cap; } Buf;

/* Append n bytes; returns 0 on success, -1 on OOM. */
static int buf_add(Buf *b, const char *s, size_t n) {
    if (b->len + n + 1 > b->cap) {
        size_t nc = b->cap ? b->cap : 256;
        while (nc < b->len + n + 1) nc *= 2;
        char *np = realloc(b->p, nc);
        if (!np) return -1;
        b->p = np; b->cap = nc;
    }
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = '\0';
    return 0;
}
static int buf_str(Buf *b, const char *s) { return buf_add(b, s, strlen(s)); }

/* Emit `text` as its own block: collapse any trailing newlines already in the
 * buffer to a single blank-line separator, write the text, then a trailing blank
 * line — so the insertion is cleanly spaced on both sides. Returns 0 / -1. */
static int emit_block(Buf *b, const char *text) {
    while (b->len > 0 && b->p[b->len-1] == '\n') b->len--;
    b->p[b->len] = '\0';
    if (b->len > 0 && buf_str(b, "\n\n")) return -1;
    if (buf_str(b, text)) return -1;
    if (b->len == 0 || b->p[b->len-1] != '\n') { if (buf_str(b, "\n")) return -1; }
    return buf_str(b, "\n");
}

/* ---- line model ----------------------------------------------------------- */

typedef struct { size_t off, full, clen; } Ln;   /* full includes the '\n'; clen excludes it */

/* Split src into lines (each carrying its trailing newline if present). */
static Ln *split_lines(const char *src, size_t len, int *nout) {
    Ln *v = NULL; int n = 0, cap = 0;
    size_t i = 0;
    while (i < len) {
        size_t start = i;
        while (i < len && src[i] != '\n') i++;
        size_t clen = i - start;
        if (i < len) i++;                 /* consume the newline */
        if (n == cap) { cap = cap ? cap * 2 : 64; v = realloc(v, (size_t)cap * sizeof *v); }
        v[n].off = start; v[n].full = i - start; v[n].clen = clen; n++;
    }
    *nout = n;
    return v;
}

/* Up to 3 leading spaces of indent (CommonMark slack). */
static size_t indent(const char *s, size_t n) {
    size_t i = 0;
    while (i < n && i < 3 && s[i] == ' ') i++;
    return i;
}

/* If the line is an ATX heading, return its level (1..6) and copy a trimmed
 * title into out[cap]; else return 0. */
static int atx_level(const char *s, size_t n, char *out, size_t cap) {
    size_t i = indent(s, n);
    int hashes = 0;
    while (i < n && s[i] == '#') { hashes++; i++; }
    if (hashes < 1 || hashes > 6) return 0;
    if (i < n && s[i] != ' ' && s[i] != '\t') return 0;   /* needs a space after # */
    while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
    size_t e = n;
    while (e > i && (s[e-1] == ' ' || s[e-1] == '\t')) e--;
    while (e > i && s[e-1] == '#') e--;
    while (e > i && (s[e-1] == ' ' || s[e-1] == '\t')) e--;
    size_t tl = e - i; if (tl >= cap) tl = cap - 1;
    memcpy(out, s + i, tl); out[tl] = '\0';
    return hashes;
}

/* Does the line open or close a fenced code block (``` or ~~~)? */
static int is_fence(const char *s, size_t n) {
    size_t i = indent(s, n);
    if (n - i < 3) return 0;
    return (s[i] == '`' && s[i+1] == '`' && s[i+2] == '`') ||
           (s[i] == '~' && s[i+1] == '~' && s[i+2] == '~');
}

char *edit_section(const char *src, size_t len, const char *anchor, EditOp op, const char *text) {
    int nline;
    Ln *L = split_lines(src, len, &nline);

    /* Find the target heading and the section end (next heading of level <= it),
     * ignoring headings inside fenced code blocks. */
    int target = -1, level = 0, end = nline, fence = 0;
    char title[512];
    for (int i = 0; i < nline; i++) {
        const char *s = src + L[i].off;
        if (is_fence(s, L[i].clen)) { fence = !fence; continue; }
        if (fence) continue;
        int lv = atx_level(s, L[i].clen, title, sizeof title);
        if (!lv) continue;
        if (target < 0) {
            if (section_title_matches(title, anchor)) { target = i; level = lv; }
        } else if (lv <= level) {
            end = i; break;
        }
    }
    if (target < 0) { free(L); return NULL; }   /* heading not found */

    /* The line index before which the payload is inserted, and the body range
     * that REPLACE drops. */
    int insert_at = (op == EDIT_INSERT) ? target + 1 : end;     /* APPEND uses end */
    int drop_from = -1, drop_to = -1;
    if (op == EDIT_REPLACE) { drop_from = target + 1; drop_to = end; insert_at = target + 1; }

    Buf b = {0};
    int ok = 0;
    for (int i = 0; i <= nline; i++) {
        if (i == insert_at) {
            if (emit_block(&b, text)) goto done;
        }
        if (i == nline) break;
        if (op == EDIT_REPLACE && i >= drop_from && i < drop_to) continue;   /* drop old body */
        if (buf_add(&b, src + L[i].off, L[i].full)) goto done;
    }
    ok = 1;
done:
    free(L);
    if (!ok) { free(b.p); return NULL; }
    if (!b.p) b.p = calloc(1, 1);   /* empty document edge case */
    return b.p;
}

/* ---- frontmatter ---------------------------------------------------------- */

/* Is the line exactly a frontmatter fence "---" (optionally trailing spaces)? */
static int is_fm_fence(const char *s, size_t clen) {
    if (clen < 3 || s[0] != '-' || s[1] != '-' || s[2] != '-') return 0;
    for (size_t i = 3; i < clen; i++) if (s[i] != ' ' && s[i] != '\t') return 0;
    return 1;
}

char *edit_frontmatter(const char *src, size_t len, const char *key, const char *value) {
    int nline;
    Ln *L = split_lines(src, len, &nline);

    /* A frontmatter block exists if line 0 is "---" and a later "---" closes it. */
    int has_fm = (nline > 0 && is_fm_fence(src + L[0].off, L[0].clen));
    int close = -1;
    if (has_fm)
        for (int i = 1; i < nline; i++)
            if (is_fm_fence(src + L[i].off, L[i].clen)) { close = i; break; }
    if (has_fm && close < 0) has_fm = 0;   /* unterminated: treat as none */

    size_t klen = strlen(key);
    Buf b = {0};
    int ok = 0;

    if (!has_fm) {
        /* Prepend a fresh block. */
        if (buf_str(&b, "---\n") || buf_str(&b, key) || buf_str(&b, ": ") ||
            buf_str(&b, value) || buf_str(&b, "\n---\n\n") ||
            buf_add(&b, src, len)) goto done;
        ok = 1; goto done;
    }

    /* Walk the existing block, replacing the key's line if present. */
    int replaced = 0;
    for (int i = 0; i < nline; i++) {
        if (i == close) {                          /* before the closing fence */
            if (!replaced) {
                if (buf_str(&b, key) || buf_str(&b, ": ") || buf_str(&b, value) || buf_str(&b, "\n")) goto done;
            }
            if (buf_add(&b, src + L[i].off, L[i].full)) goto done;
            continue;
        }
        const char *s = src + L[i].off;
        size_t cl = L[i].clen;
        int is_key = (i > 0 && i < close && cl > klen &&
                      strncmp(s, key, klen) == 0 && s[klen] == ':');
        if (is_key) {
            if (buf_str(&b, key) || buf_str(&b, ": ") || buf_str(&b, value) || buf_str(&b, "\n")) goto done;
            replaced = 1;
        } else {
            if (buf_add(&b, src + L[i].off, L[i].full)) goto done;
        }
    }
    ok = 1;
done:
    free(L);
    if (!ok) { free(b.p); return NULL; }
    if (!b.p) b.p = calloc(1, 1);
    return b.p;
}
