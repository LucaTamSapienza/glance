/* Unit tests for section.c — anchor matching and subtree extraction. */
#include "../src/section.h"
#include "../src/render.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Render markdown at a huge width so nothing wraps. */
static Doc *mk(const char *md) { return render_doc(md, strlen(md), 100000, 1); }

int main(void) {
    const char *md =
        "# Title\n\nIntro para.\n\n"
        "## Alpha\n\nalpha body\n\n"
        "## Beta\n\nbeta body\n\n"
        "### Beta Sub\n\nsub body\n";
    Doc *d = mk(md);

    /* exact, case-insensitive match; stops at the next same-level heading */
    Section a = section_find(d, "alpha");
    assert(a.found && a.level == 2);
    char *at = section_text(d, a.start, a.end);
    assert(strstr(at, "alpha body"));
    assert(!strstr(at, "beta body"));
    free(at);

    /* slug match: "Beta Sub" resolves from "beta-sub" */
    Section bs = section_find(d, "beta-sub");
    assert(bs.found && bs.level == 3);
    char *bst = section_text(d, bs.start, bs.end);
    assert(strstr(bst, "sub body"));
    free(bst);

    /* a section includes its deeper subsections */
    Section b = section_find(d, "Beta");
    assert(b.found && b.level == 2);
    char *bt = section_text(d, b.start, b.end);
    assert(strstr(bt, "beta body"));
    assert(strstr(bt, "sub body"));
    free(bt);

    /* an H1 subtree runs to the end of the document */
    Section title = section_find(d, "Title");
    assert(title.found && title.level == 1);
    assert(title.end == (int)d->nline);

    /* no match */
    Section none = section_find(d, "Nonexistent");
    assert(!none.found);

    /* empty anchor selects the whole document */
    Section whole = section_find(d, "");
    assert(whole.found && whole.start == 0 && whole.end == (int)d->nline);

    doc_free(d);
    printf("all section tests passed\n");
    return 0;
}
