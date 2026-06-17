/* legend.h — layout logic for the Reader's hidable keybinding sidebar.
 *
 * The notcurses drawing lives in tui.c; everything that can be reasoned about
 * without a terminal (the panel/content width split, the too-narrow fallback,
 * and formatting one aligned key->action row) lives here so it can be unit
 * tested. Content is ASCII, so a display column equals a byte throughout. */
#ifndef LEGEND_H
#define LEGEND_H

#include <stddef.h>

#define LEGEND_W            30   /* panel width in columns when open */
#define LEGEND_MIN_CONTENT  24   /* min columns the document keeps beside it */
#define LEGEND_KEYCOL        8   /* width of the key column inside the panel */

/* One line of the panel. key == NULL marks a non-key line: a section header
 * when action is non-empty, or a blank spacer when action is "" / NULL. */
typedef struct { const char *key; const char *action; } LegendRow;

/* The Reader-mode key list (the only mode the panel opens in) and its length. */
extern const LegendRow LEGEND_READER[];
extern const int       LEGEND_READER_N;

/* Columns left for the document: the full width, or width minus the panel when
 * the legend is open. Never negative. */
int legend_content_cols(int total_cols, int open, int panel_w);

/* True when the terminal is too narrow to reflow the document beside the panel,
 * so the caller should fall back to a centered overlay instead. */
int legend_should_overlay(int total_cols, int panel_w, int min_content);

/* Format one row into out (NUL-terminated), space-padded to exactly `inner`
 * display columns (clamped to cap-1). A key row reads " key<pad> action" with
 * the action starting one column past a `keycol`-wide key field; a header puts
 * its text at column 1; a spacer is all spaces. Over-long fields are truncated.
 * Returns the number of columns written. */
int legend_format_row(char *out, size_t cap, const LegendRow *row,
                      int inner, int keycol);

#endif
