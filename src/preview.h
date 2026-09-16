#ifndef GLANCE_PREVIEW_H
#define GLANCE_PREVIEW_H
#include <stddef.h>

/* Render through the shared HTML sink, following native polarity by default.
 * Explicit themes may use configured palettes. Returns
 * allocated HTML; actual_dark receives the resolved theme polarity. */
char *preview_html(const char *src, size_t len, const char *title,
                   const char *theme_name, int system_dark, int *actual_dark);

/* Parse FILE --ui or --ui FILE: 1 on success, -1 for invalid UI arguments,
 * 0 when this is not a UI request. The returned path points into argv. */
int preview_args(int argc, char **argv, const char **path);

/* Find the companion app beside an executable or in its installation prefix.
 * Returns an allocated absolute path, or NULL when the app is missing. */
char *preview_app_path(const char *executable);

/* Open a readable Markdown file in the macOS preview app and return promptly.
 * Returns 0 once Launch Services accepts the request, nonzero on error. */
int preview_open(const char *path, const char *theme_name);

#endif
