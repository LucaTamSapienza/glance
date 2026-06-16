/* render_test.c — tests for renderer features that need the whole Doc:
 * table column alignment (and, later, source-line attribution). Works on the
 * ANSI output with the escape codes stripped, so it checks the visible layout. */
#include "../src/render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0;
static void expect(int cond, const char *msg) {
    if (!cond) { printf("FAIL: %s\n", msg); fails++; }
}

/* Render `md` and return its plain text (ANSI stripped); caller frees. */
static char *plain(const char *md) {
    Doc *d = render_doc(md, strlen(md), 60, 1);
    char *ansi = doc_to_ansi(d);
    char *out = malloc(strlen(ansi) + 1);
    size_t o = 0;
    for (size_t i = 0; ansi[i]; ) {
        if (ansi[i] == '\x1b' && ansi[i+1] == '[') {       /* skip CSI ... m */
            i += 2; while (ansi[i] && ansi[i] != 'm') i++;
            if (ansi[i]) i++;
        } else out[o++] = ansi[i++];
    }
    out[o] = '\0';
    free(ansi); doc_free(d);
    return out;
}

/* Count display columns in a line (one per UTF-8 lead byte; our glyphs are all
 * width 1). */
static int cols(const char *s, size_t n) {
    int c = 0;
    for (size_t i = 0; i < n; i++) if ((s[i] & 0xC0) != 0x80) c++;
    return c;
}

/* Count leading / trailing ASCII spaces of a field. */
static void pad_of(const char *s, size_t n, int *lead, int *trail) {
    size_t i = 0, j = n;
    while (i < n && s[i] == ' ') i++;
    while (j > i && s[j-1] == ' ') j--;
    *lead = (int)i; *trail = (int)(n - j);
}

#define VBAR "\xe2\x94\x82"   /* │ */

int main(void) {
    char *p = plain("| Left | Mid | Right |\n|:-----|:---:|------:|\n| a | b | c |\n");

    /* Borders are drawn. */
    expect(strstr(p, "\xe2\x94\x8c") != NULL, "table has a top-left corner");
    expect(strstr(p, "\xe2\x94\x98") != NULL, "table has a bottom-right corner");
    expect(strstr(p, "\xe2\x94\xbc") != NULL, "table has a header/body junction");

    /* Collect the table's lines and check they all share one width. */
    int width = -1, consistent = 1, nlines = 0;
    for (char *line = p; *line; ) {
        char *nl = strchr(line, '\n');
        size_t len = nl ? (size_t)(nl - line) : strlen(line);
        if (memmem(line, len, "\xe2\x94\x80", 3) || memmem(line, len, VBAR, 3)) {  /* a table line */
            int w = cols(line, len);
            if (width < 0) width = w; else if (w != width) consistent = 0;
            nlines++;
        }
        if (!nl) break;
        line = nl + 1;
    }
    expect(nlines >= 5, "table emitted borders + header + 2 body rows");
    expect(consistent, "every table line has the same width (columns aligned)");

    /* On the data row, infer each cell's alignment from its padding. */
    char *row = strstr(p, VBAR " a ");
    expect(row != NULL, "found the data row");
    if (row) {
        char *nl = strchr(row, '\n');
        size_t len = nl ? (size_t)(nl - row) : strlen(row);
        /* split the row into fields between the │ bars */
        int li = 0, ti = 0, field = 0;
        char *seg = row;
        for (char *q = row; q < row + len; ) {
            if (memcmp(q, VBAR, 3) == 0) {
                if (q > seg) {
                    int lead, trail; pad_of(seg, (size_t)(q - seg), &lead, &trail);
                    if (field == 0) expect(trail > lead, "left column is left-aligned");
                    if (field == 1) { li = lead; ti = trail; expect(li >= 1 && ti >= 1 && abs(li - ti) <= 1, "middle column is centered"); }
                    if (field == 2) expect(lead > trail, "right column is right-aligned");
                    field++;
                }
                q += 3; seg = q;
            } else q++;
        }
        expect(field >= 3, "row had three cells");
    }
    free(p);

    if (fails) { printf("%d render test(s) FAILED\n", fails); return 1; }
    printf("all render tests passed\n");
    return 0;
}
