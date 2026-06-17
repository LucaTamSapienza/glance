/* Unit tests for edit.c — surgical source edits. */
#include "../src/edit.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Index of `needle` in `hay`, or -1. */
static long idx(const char *hay, const char *needle) {
    const char *p = strstr(hay, needle);
    return p ? (long)(p - hay) : -1;
}

int main(void) {
    const char *doc =
        "# Title\n\nintro\n\n"
        "## Tasks\n\n- one\n- two\n\n"
        "## Notes\n\nnote body\n";

    /* APPEND: new line lands inside Tasks, before the Notes heading. */
    {
        char *out = edit_section(doc, strlen(doc), "Tasks", EDIT_APPEND, "- three");
        assert(out);
        long t3 = idx(out, "- three"), notes = idx(out, "## Notes");
        assert(t3 >= 0 && notes >= 0 && t3 < notes);     /* inside Tasks */
        assert(idx(out, "- two") < t3);                  /* after existing items */
        assert(idx(out, "note body") >= 0);              /* Notes untouched */
        free(out);
    }

    /* INSERT: lands right after the heading, before existing items. */
    {
        char *out = edit_section(doc, strlen(doc), "Tasks", EDIT_INSERT, "- zero");
        assert(out);
        long h = idx(out, "## Tasks"), z = idx(out, "- zero"), one = idx(out, "- one");
        assert(h < z && z < one);
        free(out);
    }

    /* REPLACE: body swapped, heading kept, next section intact. */
    {
        char *out = edit_section(doc, strlen(doc), "Tasks", EDIT_REPLACE, "- done");
        assert(out);
        assert(idx(out, "## Tasks") >= 0);
        assert(idx(out, "- done") >= 0);
        assert(idx(out, "- one") < 0 && idx(out, "- two") < 0);   /* old body gone */
        assert(idx(out, "note body") >= 0);                       /* Notes intact */
        free(out);
    }

    /* slug anchor resolves too. */
    {
        char *out = edit_section(doc, strlen(doc), "tasks", EDIT_APPEND, "- via slug");
        assert(out && idx(out, "- via slug") >= 0);
        free(out);
    }

    /* Missing heading → NULL. */
    assert(edit_section(doc, strlen(doc), "Nonexistent", EDIT_APPEND, "x") == NULL);

    /* A heading inside a fenced code block must not be matched. */
    {
        const char *fenced =
            "# Real\n\n```\n## Fake\ncode\n```\n\nbody\n";
        assert(edit_section(fenced, strlen(fenced), "Fake", EDIT_APPEND, "x") == NULL);
        char *out = edit_section(fenced, strlen(fenced), "Real", EDIT_APPEND, "added");
        assert(out && idx(out, "added") >= 0);
        assert(idx(out, "## Fake") >= 0);   /* fenced text preserved verbatim */
        free(out);
    }

    /* Frontmatter: update an existing key. */
    {
        const char *fm = "---\ntitle: Old\nstatus: draft\n---\n\nbody\n";
        char *out = edit_frontmatter(fm, strlen(fm), "status", "done");
        assert(out);
        assert(idx(out, "status: done") >= 0);
        assert(idx(out, "status: draft") < 0);
        assert(idx(out, "title: Old") >= 0);   /* other keys untouched */
        free(out);
    }

    /* Frontmatter: add a new key to an existing block. */
    {
        const char *fm = "---\ntitle: T\n---\n\nbody\n";
        char *out = edit_frontmatter(fm, strlen(fm), "tags", "project");
        assert(out);
        assert(idx(out, "tags: project") >= 0);
        long tags = idx(out, "tags: project"), close = idx(out, "\n---");
        assert(tags >= 0 && tags < close + 4);   /* inside the block */
        free(out);
    }

    /* Frontmatter: create a block when none exists. */
    {
        const char *plain = "# Heading\n\nbody\n";
        char *out = edit_frontmatter(plain, strlen(plain), "status", "new");
        assert(out);
        assert(strncmp(out, "---\n", 4) == 0);
        assert(idx(out, "status: new") >= 0);
        assert(idx(out, "# Heading") > idx(out, "status: new"));   /* body follows */
        free(out);
    }

    printf("all edit tests passed\n");
    return 0;
}
