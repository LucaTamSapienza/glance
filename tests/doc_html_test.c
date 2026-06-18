/* doc_html_test.c — unit tests for the Markdown -> HTML export sink. */
#include "../src/doc_html.h"
#include "../src/theme.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int fails = 0;

/* Assert the rendered HTML contains `needle`. */
static void has(const char *html, const char *needle, const char *msg) {
    if (!strstr(html, needle)) { printf("FAIL: %s (missing: %s)\n", msg, needle); fails++; }
}

/* Assert the rendered HTML does NOT contain `needle`. */
static void hasnt(const char *html, const char *needle, const char *msg) {
    if (strstr(html, needle)) { printf("FAIL: %s (unexpected: %s)\n", msg, needle); fails++; }
}

static char *render(const char *md) {
    const Theme *t = theme_auto(1);
    char *h = md_to_html(md, strlen(md), t, "T");
    if (!h) { printf("FAIL: md_to_html returned NULL\n"); fails++; exit(1); }
    return h;
}

int main(void) {
    /* Document scaffolding + themed stylesheet. */
    char *h = render("# Title\n\ntext\n");
    has(h, "<!DOCTYPE html>", "doctype present");
    has(h, "<style>", "embedded stylesheet");
    has(h, "<title>T</title>", "title element");
    has(h, "<h1>Title</h1>", "h1 rendered");
    has(h, "<p>text</p>", "paragraph rendered");
    free(h);

    /* Inline spans and links. */
    h = render("**bold** and *em* and `co` and [x](http://e.org)\n");
    has(h, "<strong>bold</strong>", "strong");
    has(h, "<em>em</em>", "em");
    has(h, "<code>co</code>", "inline code");
    has(h, "<a href=\"http://e.org\">x</a>", "link href");
    free(h);

    /* HTML escaping of text content. */
    h = render("a < b & c > d\n");
    has(h, "a &lt; b &amp; c &gt; d", "text escaped");
    free(h);

    /* Fenced code block: highlighted, with the language tokenised. */
    h = render("```c\nint x = 1; // hi\n```\n");
    has(h, "<pre><code>", "code block wrapper");
    has(h, "hl-", "syntax highlight classes emitted");
    free(h);

    /* Unknown-language fence: escaped, no crash, still a code block. */
    h = render("```\n<tag>\n```\n");
    has(h, "<pre><code>", "plain code block wrapper");
    has(h, "&lt;tag&gt;", "code content escaped");
    free(h);

    /* GitHub table with alignment. */
    h = render("| A | B |\n|:--|--:|\n| 1 | 2 |\n");
    has(h, "<table>", "table");
    has(h, "<th", "header cell");
    has(h, "<td", "body cell");
    has(h, "text-align:right", "right alignment honoured");
    free(h);

    /* Task list. */
    h = render("- [x] done\n- [ ] todo\n");
    has(h, "type=\"checkbox\"", "task checkboxes");
    has(h, "checked", "completed task checked");
    free(h);

    /* Wikilink. */
    h = render("see [[Other Note]]\n");
    has(h, "class=\"wikilink\"", "wikilink class");
    has(h, "Other Note", "wikilink target/text");
    free(h);

    /* Blockquote. */
    h = render("> quoted\n");
    has(h, "<blockquote>", "blockquote");
    free(h);

    /* Tight list must not wrap items in <p>. */
    h = render("- one\n- two\n");
    hasnt(h, "<li><p>", "tight list items not paragraph-wrapped");
    free(h);

    if (fails) { printf("%d doc_html test(s) FAILED\n", fails); return 1; }
    printf("all doc_html tests passed\n");
    return 0;
}
