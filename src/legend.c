 /* legend.c — pure layout logic for the Reader keybinding sidebar (see legend.h).
 * No notcurses, no md4c: just width arithmetic and ASCII row formatting. */
#include "legend.h"

/* The Reader-mode bindings, grouped. Keep the action strings short enough to
 * fit the action field (LEGEND_W - 2 - LEGEND_KEYCOL - 2 columns). */
const LegendRow LEGEND_READER[] = {
    { NULL,      "Navigate"     },
    { "j k",     "move line"    },
    { "g G",     "top / bottom" },
    { "C-d C-u", "half page"    },
    { "/ n N",   "search"       },
    { NULL,      ""             },
    { NULL,      "Document"     },
    { "t",       "contents"     },
    { "Enter",   "follow link"  },
    { "b",       "backlinks"    },
    { "C-g",     "graph"        },
    { NULL,      ""             },
    { NULL,      "View"         },
    { "T",       "themes"       },
    { "?",       "this legend"  },
    { NULL,      ""             },
    { NULL,      "Edit & files" },
    { "i",       "insert mode"  },
    { "e",       "split editor" },
    { "C-s",     "save"         },
    { ":w :q",   "write / quit" },
};
const int LEGEND_READER_N = (int)(sizeof LEGEND_READER / sizeof LEGEND_READER[0]);

/* Columns left for the document beside the panel; never negative. */
int legend_content_cols(int total_cols, int open, int panel_w) {
    if (!open) return total_cols;
    int c = total_cols - panel_w;
    return c < 0 ? 0 : c;
}

/* Too narrow to reflow: prefer a centered overlay instead. */
int legend_should_overlay(int total_cols, int panel_w, int min_content) {
    return total_cols < panel_w + min_content;
}

/* Copy src into out[start..) without exceeding `limit` columns or `inner`. */
static void place(char *out, int start, int inner, int limit, const char *src) {
    if (!src) return;
    for (int i = 0; src[i] && i < limit && start + i < inner; i++)
        out[start + i] = src[i];
}

int legend_format_row(char *out, size_t cap, const LegendRow *row,
                      int inner, int keycol) {
    if (cap == 0) return 0;
    if (inner < 0) inner = 0;
    if ((size_t)inner > cap - 1) inner = (int)cap - 1;
    if (keycol < 0) keycol = 0;

    for (int i = 0; i < inner; i++) out[i] = ' ';
    out[inner] = '\0';
    if (!row) return inner;

    if (!row->key) {                       /* header or spacer */
        place(out, 1, inner, inner, row->action);
        return inner;
    }

    /* " key<pad to keycol> action" */
    place(out, 1, inner, keycol, row->key);
    int astart = 1 + keycol + 1;           /* one space after the key field */
    place(out, astart, inner, inner, row->action);
    return inner;
}
