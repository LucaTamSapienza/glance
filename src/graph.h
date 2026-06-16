#ifndef GLANCE_GRAPH_H
#define GLANCE_GRAPH_H

/* The link graph of a vault: nodes are .md files (paths relative to the vault
 * root), edges are resolved Markdown/[[wiki]] links between them. Built once and
 * shared by the JSON export (agent.c) and the in-terminal graph explorer
 * (tui.c). */

typedef struct { int from, to, wiki; } GEdge;

typedef struct {
    char  **node;   /* node names (relative paths), owned */
    int     nn;
    GEdge  *edge;
    int     ne, ecap;
} Graph;

/* Build the graph by scanning `root` recursively. `g` is zero-initialised by
 * the caller (or cleared here). */
void graph_build(const char *root, Graph *g);

void graph_free(Graph *g);

/* Index of the node matching `path_or_name` by stem (case-insensitive), or -1. */
int graph_find(const Graph *g, const char *path_or_name);

#endif /* GLANCE_GRAPH_H */
