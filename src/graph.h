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

/* Spread activation outward from `seed` (one weight per node, length g->nn) along
 * the link graph for `khop` hops. Each hop a node hands a fraction `alpha`
 * (0 < alpha < 1) of its current weight to its neighbours, split by its degree so
 * a densely-linked hub does not dominate; edges are treated as undirected. `out`
 * (length g->nn, caller-owned) receives the weight that *arrives* at each node
 * summed over hops 1..khop — i.e. relevance propagated in from the seeds, the
 * seed's own weight excluded. This is what surfaces a note that no keyword or
 * embedding matched but that is linked to strong matches. khop<=0 yields zeros. */
void graph_expand(const Graph *g, const double *seed, int khop, double alpha,
                  double *out);

#endif /* GLANCE_GRAPH_H */
