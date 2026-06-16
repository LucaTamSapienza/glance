/* highlight_test.c — unit tests for the spec-driven code highlighter.
 *
 * Two properties matter: (1) the emitted spans tile the whole line (so the code
 * box renders without gaps), and (2) tokens land in the right class. */
#include "../src/highlight.h"

#include <stdio.h>
#include <string.h>

static int fails = 0;
static void expect(int cond, const char *msg) {
    if (!cond) { printf("FAIL: %s\n", msg); fails++; }
}

/* capture of one run from hl_line */
typedef struct { HLKind k; char t[128]; } Span;
static Span spans[256];
static int  nspan;
static char concat[1024];
static int  clen;

static void cap(void *ud, HLKind k, const char *s, size_t n) {
    (void)ud;
    if (n >= sizeof spans[0].t) n = sizeof spans[0].t - 1;
    spans[nspan].k = k;
    memcpy(spans[nspan].t, s, n); spans[nspan].t[n] = 0;
    nspan++;
    memcpy(concat + clen, s, n); clen += (int)n;
}

/* Run the highlighter over one line, resetting capture state. */
static void run(int lang, HLState *st, const char *s) {
    nspan = 0; clen = 0; concat[0] = 0;
    hl_line(lang, st, s, strlen(s), cap, NULL);
    concat[clen] = 0;
}

/* True if some captured span has exactly this kind and text. */
static int has(HLKind k, const char *t) {
    for (int i = 0; i < nspan; i++)
        if (spans[i].k == k && strcmp(spans[i].t, t) == 0) return 1;
    return 0;
}

int main(void) {
    HLState st;

    /* Unknown languages resolve to -1 and render as one plain span. */
    expect(hl_lang("cobol", 5) == -1, "unknown language -> -1");
    expect(hl_lang("python", 6) >= 0, "python resolves");
    expect(hl_lang("PY", 2) == hl_lang("python3", 7), "aliases + case fold to one id");

    /* Coverage invariant: spans always reconcatenate to the input. */
    const char *samples[] = {
        "def f(x):  # comment", "key: value  # note", "echo $HOME && ls -la",
        "int n = 0x1F; /* c */", "let s = `tmpl`;", "" };
    int langs[] = { hl_lang("py",2), hl_lang("yaml",4), hl_lang("sh",2),
                    hl_lang("c",1), hl_lang("js",2), hl_lang("c",1) };
    for (int i = 0; samples[i][0] || i == 5; i++) {
        memset(&st, 0, sizeof st);
        run(langs[i], &st, samples[i]);
        expect(strcmp(concat, samples[i]) == 0, "spans tile the whole line");
        if (i == 5) break;
    }

    /* Python: keyword, function call, comment. */
    memset(&st, 0, sizeof st);
    run(hl_lang("python", 6), &st, "def greet(name):  # hi");
    expect(has(HL_KEYWORD, "def"), "python def is a keyword");
    expect(has(HL_FUNCTION, "greet"), "name before ( is a function");
    expect(has(HL_COMMENT, "# hi"), "python # comment");

    /* Strings and numbers. */
    memset(&st, 0, sizeof st);
    run(hl_lang("python", 6), &st, "x = \"hello\" + 42");
    expect(has(HL_STRING, "\"hello\""), "double-quoted string");
    expect(has(HL_NUMBER, "42"), "decimal number");

    /* YAML: mapping key as property, boolean as keyword, comment. */
    memset(&st, 0, sizeof st);
    run(hl_lang("yaml", 4), &st, "enabled: true  # on");
    expect(has(HL_PROPERTY, "enabled"), "yaml key is a property");
    expect(has(HL_KEYWORD, "true"), "yaml boolean is a keyword");
    expect(has(HL_COMMENT, "# on"), "yaml # comment");

    /* Shell: keyword + $variable. */
    memset(&st, 0, sizeof st);
    run(hl_lang("bash", 4), &st, "echo $HOME");
    expect(has(HL_KEYWORD, "echo"), "shell echo is a keyword");
    expect(has(HL_VARIABLE, "$HOME"), "shell $variable");

    /* Multi-line C block comment: state carries across lines. */
    memset(&st, 0, sizeof st);
    run(hl_lang("c", 1), &st, "x /* open");
    expect(st.in_block_comment, "unterminated block comment sets state");
    expect(has(HL_COMMENT, "/* open"), "open block comment to EOL is a comment");
    run(hl_lang("c", 1), &st, "still */ y = 1;");
    expect(!st.in_block_comment, "block comment closes on next line");
    expect(has(HL_COMMENT, "still */"), "block comment tail is a comment");
    expect(has(HL_NUMBER, "1"), "code after block comment is highlighted");

    /* Multi-line Python triple-quoted string. */
    memset(&st, 0, sizeof st);
    run(hl_lang("python", 6), &st, "s = \"\"\"doc");
    expect(st.in_triple, "unterminated triple string sets state");
    run(hl_lang("python", 6), &st, "more\"\"\" + 1");
    expect(!st.in_triple, "triple string closes on next line");
    expect(has(HL_NUMBER, "1"), "code after triple string is highlighted");

    if (fails) { printf("%d highlight test(s) FAILED\n", fails); return 1; }
    printf("all highlight tests passed\n");
    return 0;
}
