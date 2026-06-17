#ifndef GLANCE_EDIT_H
#define GLANCE_EDIT_H

#include <stddef.h>

/* edit.c — surgical, structure-addressed edits on raw Markdown source.
 *
 * The agent declares intent + location (a heading anchor, or a frontmatter key);
 * these functions return a NEW document string with the change applied and all
 * other formatting preserved. They work on the raw source — not the rendered
 * Doc — so the round-trip is exact. Writing the result to disk atomically is the
 * caller's job (agent.c -> atomic_write). Pure and unit-tested. */

typedef enum { EDIT_APPEND, EDIT_INSERT, EDIT_REPLACE } EditOp;

/* Apply `op` to the section under heading `anchor` (matched by text or slug) in
 * the `len`-byte Markdown `src`, with `text` as the payload:
 *   EDIT_APPEND  — add `text` at the end of the section (before the next heading)
 *   EDIT_INSERT  — add `text` right after the heading line
 *   EDIT_REPLACE — replace the section body (keeping the heading) with `text`
 * ATX headings only; lines inside fenced code blocks are not treated as
 * headings. Returns a malloc'd new document (caller frees), or NULL if the
 * heading is not found or on OOM. */
char *edit_section(const char *src, size_t len, const char *anchor, EditOp op, const char *text);

/* Set YAML frontmatter key `key` to `value` in `src`: update the key if present,
 * add it to an existing leading `---` block, or create a new frontmatter block
 * at the top. Returns a malloc'd new document (caller frees), or NULL on OOM. */
char *edit_frontmatter(const char *src, size_t len, const char *key, const char *value);

#endif /* GLANCE_EDIT_H */
