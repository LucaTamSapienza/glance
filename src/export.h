#ifndef GLANCE_EXPORT_H
#define GLANCE_EXPORT_H

#include "theme.h"

/* export.h — write a Markdown file out as HTML or PDF.
 *
 * The format is chosen from `out`'s extension: ".pdf" produces a PDF, anything
 * else (".html"/".htm"/none) produces HTML. HTML is rendered in-process by the
 * doc_html sink; PDF is HTML handed to the first available external converter
 * (weasyprint / wkhtmltopdf / headless Chrome), since rendering PDF in C would
 * be out of proportion to the feature.
 *
 * Returns 0 on success; prints a diagnostic and returns non-zero on failure
 * (read error, write error, or — for PDF — no converter found). */
int export_file(const char *in, const char *out, const Theme *theme);

/* True if `out` names a PDF target (case-insensitive ".pdf" suffix). */
int export_wants_pdf(const char *out);

#endif /* GLANCE_EXPORT_H */
