#ifndef GLANCE_RENDER_MD_H
#define GLANCE_RENDER_MD_H

#include <stddef.h>

/* Render Markdown `src` (UTF-8, `len` bytes) to an ANSI-styled string wrapped
 * to `width` columns for a dark (dark=1) or light (dark=0) terminal.
 *
 * Returns a newly malloc'd, NUL-terminated string the caller must free(), or
 * NULL on allocation failure. This is glance's own renderer: it replaces
 * glamour. md4c parses; the callbacks here decide exactly what ANSI we emit,
 * so nothing about the output is opaque to us.
 */
char *render_markdown(const char *src, size_t len, int width, int dark);

#endif /* GLANCE_RENDER_MD_H */
