#ifndef GLANCE_AGENT_H
#define GLANCE_AGENT_H

#include <stddef.h>

/* Agent-facing structured exports. These print JSON to stdout so tools and LLM
 * agents can consume a Markdown file's structure (or a whole vault's link graph)
 * without rendering or a server — just `glance --graph .`. */

/* Print the heading outline of `src` as a JSON array of {level,title,line}. */
void agent_outline(const char *src, size_t len);

/* Like agent_outline, but bounded and richer: `depth` > 0 keeps only headings of
 * that level or shallower; `abstract` adds each heading's first paragraph as an
 * "abstract" field. The token-cheap, progressively-disclosable map of a note. */
void agent_outline_ex(const char *src, size_t len, int depth, int abstract);

/* Print the link-graph neighbourhood of `note` within the vault at `dir` out to
 * `depth` hops (<=0 means 1) as {note,found,depth,neighbors:[{note,distance,
 * direction}]}, where direction is outbound/backlink/both for direct neighbours
 * and "path" further out. Returns 0 on success, non-zero if dir is unreadable. */
int agent_neighbors(const char *dir, const char *note, int depth);

/* Print the section of `src` addressed by `anchor` (a heading text or slug; NULL
 * = whole document) as a JSON object {anchor,found,level,start_line,end_line,
 * text,receipt}, where receipt accounts the section's tokens versus a whole-file
 * read. This is the token-cheap bounded read behind `glance --section`. */
void agent_section(const char *src, size_t len, const char *anchor);

/* Print the links in `src` as a JSON array of {target,wiki}. */
void agent_links(const char *src, size_t len);

/* Print the notes in the vault at `dir` that link to `note` (resolved by stem)
 * as {note,backlinks:[{note[,context]}]}; with `want_context`, each backlink
 * carries the source line that mentions the note. Returns 0 on success,
 * non-zero if dir is unreadable. */
int agent_backlinks(const char *dir, const char *note, int want_context);

/* Print the notes in the vault at `dir` modified after the Unix time `since` as
 * {since,changed:[{note,mtime}]} — the cheap "what changed?" delta. Returns 0 on
 * success, non-zero if dir is unreadable. */
int agent_since(const char *dir, long since);

/* Scan `dir` for *.md files and print the link graph as {nodes,edges}. Edges
 * connect files via Markdown links and [[wikilinks]] resolved by base name.
 * Returns 0 on success, non-zero if the directory can't be read. */
int agent_graph(const char *dir);

/* Retrieve a token-cheap context bundle for `query` over the vault at `dir`:
 * rank note sections (BM25 + a graph prior), select under `budget` tokens
 * (0 = no cap), and print {query,budget_tokens,chunks,truncated,receipt} as
 * JSON. `chunks` are the included sections (full or, coarse-to-fine, abstract);
 * `truncated` is the manifest of what was left out. Returns 0 on success,
 * non-zero if the directory can't be read. */
int agent_context(const char *dir, const char *query, size_t budget);

#endif /* GLANCE_AGENT_H */
