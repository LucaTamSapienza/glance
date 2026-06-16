#ifndef GLANCE_RENDER_H
#define GLANCE_RENDER_H

#include <stddef.h>
#include <stdint.h>

/* The structured render model. glance's renderer turns Markdown into a Doc:
 * a list of visual Lines, each a sequence of styled Runs. This is the model
 * both sinks consume — the ANSI serializer (doc_to_ansi, for the CLI and
 * tests) and the notcurses blitter (slice 2+, which writes Runs straight to
 * cells). Because we build the cells ourselves there is no ANSI to strip or
 * re-inject for search/cursor work later.
 */

typedef struct { uint8_t r, g, b; } RGB;

typedef struct {
    unsigned bold      : 1;
    unsigned italic    : 1;
    unsigned underline : 1;
    unsigned strike    : 1;
    unsigned dim       : 1;
    unsigned has_fg    : 1;
    unsigned has_bg    : 1;
    RGB fg;
    RGB bg;
} Style;

typedef struct {
    char  *text;   /* UTF-8, owned */
    size_t len;
    Style  st;
    char  *link;   /* target URL if this run is part of a link, else NULL (owned) */
} Run;

typedef struct {
    Run   *runs;
    size_t nrun, cap;
    int    cols;         /* visible width used by the runs */
    int    source_line;  /* 1-based source line, or 0 if unknown (filled later) */
    int    heading;      /* heading level 1..6 on a heading's first line, else 0 */
    int    fill;         /* 1 = pad rest of width with fill_bg (code blocks) */
    RGB    fill_bg;
    char  *image;        /* image src if this line opens an image block, else NULL (owned) */
    int    img_rows;     /* reserved height of the image block (only on the image line) */
} Line;

typedef struct {
    Line  *lines;
    size_t nline, cap;
    int    width;
} Doc;

/* Parse + render `src` (UTF-8, `len` bytes) at `width` columns for a dark
 * (dark=1) or light (dark=0) terminal. Returns an owned Doc, or NULL on OOM.
 */
Doc *render_doc(const char *src, size_t len, int width, int dark);

void doc_free(Doc *d);

/* Serialize a Doc to a newly malloc'd, NUL-terminated ANSI string (caller
 * frees). Used by the render-only CLI and by tests. */
char *doc_to_ansi(const Doc *d);

#endif /* GLANCE_RENDER_H */
