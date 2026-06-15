#ifndef GLANCE_EDITOR_H
#define GLANCE_EDITOR_H

#include <stddef.h>

/* A minimal line-array text buffer — glance's replacement for bubbles/textarea.
 * The document is an array of lines (raw UTF-8 bytes, no trailing newline).
 * The cursor is a (line, byte-offset) pair; movement is rune-aware. This holds
 * no terminal state: tui.c draws it and maps cursor/scroll to cells. Keeping it
 * notcurses-free makes it unit-testable on its own.
 */

typedef struct {
    char  *b;
    size_t len, cap;
} ELine;

typedef struct {
    ELine *lines;
    size_t n, cap;
    int    cy, cx;       /* cursor: line index, byte offset within the line */
    int    top, xoff;    /* scroll: first visible line, left display-col offset */
    int    goal_col;     /* desired display col for vertical moves; -1 = recompute */
    int    dirty;        /* edited since last sync to disk/source */
} Editor;

void  editor_init(Editor *e, const char *src, size_t len);
void  editor_free(Editor *e);

/* Serialize the buffer back to a single string (lines joined with '\n').
 * Returns an owned, NUL-terminated buffer; sets *out_len to its byte length. */
char *editor_source(const Editor *e, size_t *out_len);

/* editing (s for editor_insert must contain no '\n') */
void editor_insert(Editor *e, const char *s, size_t n);
void editor_newline(Editor *e);
void editor_backspace(Editor *e);
void editor_delete(Editor *e);

/* movement */
void editor_left(Editor *e);
void editor_right(Editor *e);
void editor_up(Editor *e);
void editor_down(Editor *e);
void editor_home(Editor *e);
void editor_end(Editor *e);

/* display column of the cursor within its line (rune-count approximation) */
int editor_cursor_col(const Editor *e);

/* UTF-8 column<->byte helpers over a raw line (shared with the drawing code) */
int    ed_byte_to_col(const char *b, size_t byte);
size_t ed_col_to_byte(const char *b, size_t len, int col);

#endif /* GLANCE_EDITOR_H */
