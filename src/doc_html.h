#ifndef GLANCE_DOC_HTML_H
#define GLANCE_DOC_HTML_H

#include <stddef.h>
#include "theme.h"

/* doc_html.h — export a Markdown document to a self-contained HTML page.
 *
 * Render `src` (`len` bytes of UTF-8 Markdown) to a complete, standalone HTML
 * document string (owned; caller frees). The page is themed with `theme`
 * (colours + fenced-code syntax classes) and embeds its own CSS, so it needs no
 * external assets or JavaScript. `title` fills <title> (may be NULL → "glance").
 * Returns NULL on OOM. */
char *md_to_html(const char *src, size_t len, const Theme *theme, const char *title);

#endif /* GLANCE_DOC_HTML_H */
