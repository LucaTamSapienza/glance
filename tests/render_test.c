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

/* Source line (1-based) of the first Doc line whose text contains `needle`,
 * or -2 if not found. */
static int srcline_of(Doc *d, const char *needle) {
    for (size_t i = 0; i < d->nline; i++) {
        char buf[512]; size_t o = 0;
        for (size_t r = 0; r < d->lines[i].nrun && o < sizeof buf - 1; r++) {
            size_t l = d->lines[i].runs[r].len;
            if (o + l >= sizeof buf) l = sizeof buf - 1 - o;
            memcpy(buf + o, d->lines[i].runs[r].text, l); o += l;
        }
        buf[o] = '\0';
        if (strstr(buf, needle)) return d->lines[i].source_line;
    }
    return -2;
}

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

    /* Source-line attribution: each visual line maps back to its source line.
     * Lines are numbered 1-based; blanks separate the blocks below. */
    const char *md =
        "# Title\n"          /* 1 */
        "\n"                 /* 2 */
        "First para.\n"      /* 3 */
        "\n"                 /* 4 */
        "## Section\n"       /* 5 */
        "\n"                 /* 6 */
        "- item one\n"       /* 7 */
        "- item two\n"       /* 8 */
        "\n"                 /* 9 */
        "```go\n"            /* 10 */
        "func main() {}\n"   /* 11 */
        "```\n";             /* 12 */
    Doc *d = render_doc(md, strlen(md), 60, 1);
    expect(srcline_of(d, "Title") == 1,     "heading maps to its source line");
    expect(srcline_of(d, "First") == 3,     "paragraph maps to its source line");
    expect(srcline_of(d, "Section") == 5,   "second heading maps to its source line");
    expect(srcline_of(d, "item one") == 7,  "first list item maps to its source line");
    expect(srcline_of(d, "item two") == 8,  "second list item maps to its source line");
    expect(srcline_of(d, "func main") == 11,"code line maps to its source line");
    doc_free(d);

    /* Inline image: a placeholder line carries the src, reserves rows, and is
     * linked so the reader can open it. */
    Doc *im = render_doc("![a cat](cat.png)\n", 18, 60, 1);
    int img = -1;
    for (size_t i = 0; i < im->nline; i++) if (im->lines[i].image) { img = (int)i; break; }
    expect(img >= 0, "an image line is emitted");
    if (img >= 0) {
        expect(strcmp(im->lines[img].image, "cat.png") == 0, "image src is captured");
        expect(im->lines[img].img_rows > 1, "image reserves rows for the picture");
        int linked = 0, alt = 0;
        for (size_t r = 0; r < im->lines[img].nrun; r++) {
            Run *rn = &im->lines[img].runs[r];
            if (rn->link && strcmp(rn->link, "cat.png") == 0) linked = 1;
            if (strstr(rn->text, "cat")) alt = 1;
        }
        expect(linked, "placeholder is linked to the image (Enter opens it)");
        expect(alt, "placeholder shows the alt text");
    }
    doc_free(im);

    if (fails) { printf("%d render test(s) FAILED\n", fails); return 1; }
    printf("all render tests passed\n");
    return 0;
}
