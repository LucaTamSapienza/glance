/* editor_test.c — unit tests for the line-array editor (pure model).
 * Build: see `make test`. Runs under ASan/UBSan in CI-style checks.
 */
#include "../src/editor.h"

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static int fails = 0;

/* assert the serialized buffer equals expected */
static void expect_src(Editor *e, const char *want, const char *msg) {
    size_t n; char *got = editor_source(e, &n);
    if (strcmp(got, want) != 0) {
        printf("FAIL: %s\n  want: %s\n  got : %s\n", msg, want, got);
        fails++;
    }
    free(got);
}

static void test_init_split(void) {
    Editor e;
    editor_init(&e, "a\nb\nc", 5);
    assert(e.n == 3);
    expect_src(&e, "a\nb\nc", "init splits 3 lines");
    editor_free(&e);

    editor_init(&e, "", 0);
    assert(e.n == 1);                       /* always >= 1 line */
    expect_src(&e, "", "empty -> one empty line");
    editor_free(&e);

    editor_init(&e, "x\n", 2);
    assert(e.n == 2);                       /* trailing newline -> empty last line */
    expect_src(&e, "x\n", "trailing newline round-trips");
    editor_free(&e);
}

static void test_insert_and_newline(void) {
    Editor e;
    editor_init(&e, "hello", 5);
    e.cy = 0; e.cx = 5;
    editor_insert(&e, " world", 6);
    expect_src(&e, "hello world", "append text");
    assert(e.cx == 11);

    e.cx = 5;                               /* between "hello" and " world" */
    editor_newline(&e);
    expect_src(&e, "hello\n world", "newline splits line");
    assert(e.cy == 1 && e.cx == 0);
    editor_free(&e);
}

static void test_backspace_join(void) {
    Editor e;
    editor_init(&e, "ab\ncd", 5);
    e.cy = 1; e.cx = 0;
    editor_backspace(&e);                   /* join line 2 into line 1 */
    expect_src(&e, "abcd", "backspace at col0 joins lines");
    assert(e.cy == 0 && e.cx == 2);

    editor_backspace(&e);                   /* delete 'b' */
    expect_src(&e, "acd", "backspace deletes prev char");
    assert(e.cx == 1);
    editor_free(&e);
}

static void test_delete_forward(void) {
    Editor e;
    editor_init(&e, "ab\ncd", 5);
    e.cy = 0; e.cx = 2;                      /* end of "ab" */
    editor_delete(&e);                      /* join next line */
    expect_src(&e, "abcd", "delete at EOL joins next line");
    e.cx = 0;
    editor_delete(&e);                      /* delete 'a' */
    expect_src(&e, "bcd", "delete removes char at cursor");
    editor_free(&e);
}

static void test_utf8_movement(void) {
    Editor e;
    /* "café" = c a f é(2 bytes). Then "ñ" line. */
    editor_init(&e, "caf\xc3\xa9\n\xc3\xb1x", 9);
    e.cy = 0; e.cx = 0;
    editor_end(&e);
    assert(e.cx == 5);                      /* 4 runes, é is 2 bytes -> 5 bytes */
    assert(editor_cursor_col(&e) == 4);     /* but 4 display columns */

    editor_left(&e);                        /* over é (2 bytes) in one step */
    assert(e.cx == 3);
    assert(editor_cursor_col(&e) == 3);

    editor_backspace(&e);                   /* delete 'f' */
    expect_src(&e, "ca\xc3\xa9\n\xc3\xb1x", "utf8 backspace keeps é intact");
    editor_free(&e);
}

static void test_vertical_goal_col(void) {
    Editor e;
    editor_init(&e, "abcdef\nxy\nlongline", 18);
    e.cy = 0; e.cx = 5;                      /* col 5 on a long line */
    editor_down(&e);                         /* short line "xy": clamp to end */
    assert(e.cy == 1);
    assert(editor_cursor_col(&e) == 2);
    editor_down(&e);                         /* back to a long line: restore col 5 */
    assert(e.cy == 2);
    assert(editor_cursor_col(&e) == 5);      /* goal column preserved */
    editor_free(&e);
}

static void test_word_motion(void) {
    Editor e;
    editor_init(&e, "the quick  brown", 16);
    e.cy = 0; e.cx = 0;
    editor_word_right(&e); assert(e.cx == 3);   /* end of "the" */
    editor_word_right(&e); assert(e.cx == 9);   /* end of "quick" (double space next) */
    editor_word_right(&e); assert(e.cx == 16);  /* end of "brown" = end of line */
    editor_word_left(&e);  assert(e.cx == 11);  /* back to start of "brown" */
    editor_word_left(&e);  assert(e.cx == 4);   /* start of "quick" */
    editor_word_left(&e);  assert(e.cx == 0);   /* start of "the" */
    editor_free(&e);

    editor_init(&e, "ciao, x (qui)", 13);       /* punctuation separates words */
    e.cy = 0; e.cx = 0;
    editor_word_right(&e); assert(e.cx == 4);   /* end of "ciao", before the comma */
    editor_word_right(&e); assert(e.cx == 7);   /* end of "x" */
    editor_word_right(&e); assert(e.cx == 12);  /* end of "qui", inside the parens */
    editor_word_left(&e);  assert(e.cx == 9);   /* start of "qui" */
    editor_word_left(&e);  assert(e.cx == 6);   /* start of "x" */
    editor_word_left(&e);  assert(e.cx == 0);   /* start of "ciao" */
    editor_free(&e);

    editor_init(&e, "pi\xc3\xb9 due", 8);       /* accented letters stay in the word */
    e.cy = 0; e.cx = 0;
    editor_word_right(&e); assert(e.cx == 4);   /* end of "più" (multibyte ù) */
    editor_word_left(&e);  assert(e.cx == 0);   /* back to its start */
    editor_free(&e);

    editor_init(&e, "ab\ncd", 5);               /* word motion wraps across lines */
    e.cy = 1; e.cx = 0;
    editor_word_left(&e);  assert(e.cy == 0 && e.cx == 2);
    editor_word_right(&e); assert(e.cy == 1 && e.cx == 0);
    editor_free(&e);
}

int main(void) {
    test_init_split();
    test_insert_and_newline();
    test_backspace_join();
    test_delete_forward();
    test_utf8_movement();
    test_vertical_goal_col();
    test_word_motion();
    if (fails) { printf("%d test(s) FAILED\n", fails); return 1; }
    printf("all editor tests passed\n");
    return 0;
}
