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

/* Split src into lines (each carrying its trailing newline if present). Returns
 * NULL only on OOM (an empty document yields a valid pointer with *nout == 0). */
static Ln *split_lines(const char *src, size_t len, int *nout) {
    int n = 0, cap = 64;
    Ln *v = malloc((size_t)cap * sizeof *v);
    if (!v) { *nout = 0; return NULL; }
    size_t i = 0;
    while (i < len) {
        size_t start = i;
        while (i < len && src[i] != '\n') i++;
        size_t clen = i - start;
        if (i < len) i++;                 /* consume the newline */
        if (n == cap) {
            int nc = cap * 2;
            Ln *nv = realloc(v, (size_t)nc * sizeof *v);
            if (!nv) { free(v); *nout = 0; return NULL; }
            v = nv; cap = nc;
        }
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
/* If the line is a code-fence delimiter (a run of >=3 of the same ` or ~),
 * return 1 and report the fence char, run length, and whether an info string
 * (any non-whitespace) follows. A closing fence matches the opening char, is at
 * least as long, and carries no info string. */
static int fence_line(const char *s, size_t n, char *ch, int *runlen, int *info) {
    size_t i = indent(s, n);
    if (n - i < 3) return 0;
    char c = s[i];
    if (c != '`' && c != '~') return 0;
    size_t j = i;
    while (j < n && s[j] == c) j++;
    if (j - i < 3) return 0;
    int has = 0;
    for (size_t k = j; k < n; k++) if (s[k] != ' ' && s[k] != '\t') { has = 1; break; }
    *ch = c; *runlen = (int)(j - i); *info = has;
    return 1;
}

/* All-whitespace line? */
static int blank_src(const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) if (s[i] != ' ' && s[i] != '\t') return 0;
    return 1;
}

/* A list-item marker start, so a setext text line is not mistaken for one. */
static int is_list_start(const char *s, size_t n) {
    size_t i = indent(s, n);
    if (i < n && (s[i] == '-' || s[i] == '+' || s[i] == '*') &&
        (i + 1 >= n || s[i+1] == ' ' || s[i+1] == '\t')) return 1;
    size_t j = i;
    while (j < n && s[j] >= '0' && s[j] <= '9') j++;
    return (j > i && j < n && (s[j] == '.' || s[j] == ')'));
}

/* A setext underline: only '=' (level 1) or only '-' (level 2) after indent, at
 * least one char, nothing else. Returns 1, 2, or 0. */
static int setext_underline(const char *s, size_t n) {
    size_t i = indent(s, n), e = n;
    while (e > i && (s[e-1] == ' ' || s[e-1] == '\t')) e--;
    if (e == i) return 0;
    char c = s[i];
    if (c != '=' && c != '-') return 0;
    for (size_t k = i; k < e; k++) if (s[k] != c) return 0;
    return c == '=' ? 1 : 2;
}

/* Copy the whitespace-trimmed content of a line into out[cap]. */
static void trim_copy(const char *s, size_t n, char *out, size_t cap) {
    size_t i = 0; while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
    size_t e = n; while (e > i && (s[e-1] == ' ' || s[e-1] == '\t')) e--;
    size_t tl = e - i; if (tl >= cap) tl = cap - 1;
    memcpy(out, s + i, tl); out[tl] = '\0';
}

char *edit_section(const char *src, size_t len, const char *anchor, EditOp op, const char *text) {
    int nline;
    Ln *L = split_lines(src, len, &nline);
    if (!L) return NULL;

    /* Find the target heading and the section end (next heading of level <= it),
     * ignoring headings inside fenced code blocks. Both ATX (# ...) and setext
     * (text + ===/--- underline) headings count; a setext heading spans 2 lines. */
    int target = -1, level = 0, tspan = 1, end = nline;
    int fence = 0, flen = 0; char fch = 0;
    char title[512];
    for (int i = 0; i < nline; i++) {
        const char *s = src + L[i].off; size_t cl = L[i].clen;
        char fc; int frl, finfo;
        if (fence_line(s, cl, &fc, &frl, &finfo)) {
            if (!fence) { fence = 1; fch = fc; flen = frl; }
            else if (fc == fch && frl >= flen && !finfo) fence = 0;
            continue;                                  /* a fence line is never a heading */
        }
        if (fence) continue;                           /* inside a code block */

        int lv = atx_level(s, cl, title, sizeof title), span = 1;
        if (!lv && i + 1 < nline && !blank_src(s, cl) &&
            !is_list_start(s, cl) && !setext_underline(s, cl)) {
            int sl = setext_underline(src + L[i+1].off, L[i+1].clen);
            if (sl) { lv = sl; span = 2; trim_copy(s, cl, title, sizeof title); }
        }
        if (!lv) continue;

        if (target < 0 && section_title_matches(title, anchor)) {
            target = i; level = lv; tspan = span;
            if (span == 2) i++;                        /* consume the underline */
            continue;
        }
        if (target >= 0 && lv <= level) { end = i; break; }
        if (span == 2) i++;                            /* skip a non-target underline */
    }
    if (target < 0) { free(L); return NULL; }   /* heading not found */

    /* The line index before which the payload is inserted, and the body range
     * that REPLACE drops. A setext heading occupies tspan (2) lines. */
    int insert_at = (op == EDIT_INSERT) ? target + tspan : end;   /* APPEND uses end */
    int drop_from = -1, drop_to = -1;
    if (op == EDIT_REPLACE) { drop_from = target + tspan; drop_to = end; insert_at = target + tspan; }

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

/* Write a "key: value\n" frontmatter line into b, double-quoting the value when
 * it would otherwise be misparsed as YAML (a colon, a '#', leading/trailing
 * space, empty, or a leading indicator character). Returns 0 / -1. The value is
 * already known to be newline-free. */
static int fm_pair(Buf *b, const char *key, const char *value) {
    if (buf_str(b, key) || buf_str(b, ": ")) return -1;
    int quote = value[0] == '\0';
    for (const char *p = value; *p && !quote; p++)
        if (*p == ':' || *p == '#' || *p == '"' || *p == '\\' || *p == '\t') quote = 1;
    if (!quote) {
        size_t n = strlen(value);
        if (value[0] == ' ' || value[n-1] == ' ' || strchr("[]{}&*!|>'%@`,?-", value[0])) quote = 1;
    }
    if (!quote) return buf_str(b, value) || buf_str(b, "\n");
    if (buf_str(b, "\"")) return -1;
    for (const char *p = value; *p; p++) {
        if ((*p == '"' || *p == '\\') && buf_add(b, "\\", 1)) return -1;
        if (buf_add(b, p, 1)) return -1;
    }
    return buf_str(b, "\"\n");
}

char *edit_frontmatter(const char *src, size_t len, const char *key, const char *value) {
    if (!key || strchr(key, '\n') || strchr(key, '\r') || strchr(key, ':'))
        return NULL;                       /* a key can't contain a newline or ':' */
    if (!value || strchr(value, '\n') || strchr(value, '\r'))
        return NULL;                       /* a newline in the value would corrupt YAML */

    int nline;
    Ln *L = split_lines(src, len, &nline);
    if (!L) return NULL;

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
        if (buf_str(&b, "---\n") || fm_pair(&b, key, value) ||
            buf_str(&b, "---\n\n") || buf_add(&b, src, len)) goto done;
        ok = 1; goto done;
    }

    /* Walk the existing block, replacing the key's line if present. */
    int replaced = 0;
    for (int i = 0; i < nline; i++) {
        if (i == close) {                          /* before the closing fence */
            if (!replaced && fm_pair(&b, key, value)) goto done;
            if (buf_add(&b, src + L[i].off, L[i].full)) goto done;
            continue;
        }
        const char *s = src + L[i].off;
        size_t cl = L[i].clen;
        int is_key = (i > 0 && i < close && cl > klen &&
                      strncmp(s, key, klen) == 0 && s[klen] == ':');
        if (is_key) {
            if (fm_pair(&b, key, value)) goto done;
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
