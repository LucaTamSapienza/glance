#ifndef GLANCE_AGENT_H
#define GLANCE_AGENT_H

#include <stddef.h>

/* Agent-facing structured exports. These print JSON to stdout so tools and LLM
 * agents can consume a Markdown file's structure (or a whole vault's link graph)
 * without rendering or a server — just `glance --graph .`. */

/* Print the heading outline of `src` as a JSON array of {level,title,line}. */
void agent_outline(const char *src, size_t len);

/* Print the links in `src` as a JSON array of {target,wiki}. */
void agent_links(const char *src, size_t len);

/* Scan `dir` for *.md files and print the link graph as {nodes,edges}. Edges
 * connect files via Markdown links and [[wikilinks]] resolved by base name.
 * Returns 0 on success, non-zero if the directory can't be read. */
int agent_graph(const char *dir);

#endif /* GLANCE_AGENT_H */
