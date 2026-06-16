/* graph.c — build a vault's link graph from its files (see graph.h). */
#include "graph.h"
#include "vault.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* Index of the node whose stem matches `target`, or -1. */
int graph_find(const Graph *g, const char *target) {
    char want[256]; vault_stem(target, want, sizeof want);
    for (int i = 0; i < g->nn; i++) {
        char have[256]; vault_stem(g->node[i], have, sizeof have);
        if (strcasecmp(have, want) == 0) return i;
    }
    return -1;
}

/* Append an edge, growing the array geometrically (doubling) so building a
 * densely-linked vault's graph stays linear rather than O(edges squared). */
static void edge_push(Graph *g, int from, int to, int wiki) {
    if (g->ne == g->ecap) {
        int nc = g->ecap ? g->ecap * 2 : 64;
        GEdge *p = realloc(g->edge, (size_t)nc * sizeof(GEdge));
        if (!p) return;
        g->edge = p; g->ecap = nc;
    }
    g->edge[g->ne].from = from; g->edge[g->ne].to = to; g->edge[g->ne].wiki = wiki;
    g->ne++;
}

void graph_build(const char *root, Graph *g) {
    memset(g, 0, sizeof *g);

    VFiles f = {0};
    vault_scan(root, &f);
    g->nn = f.n;
    g->node = malloc(sizeof(char *) * (f.n ? f.n : 1));
    for (int i = 0; i < f.n; i++) g->node[i] = strdup(f.v[i]);

    for (int i = 0; i < g->nn; i++) {
        char path[8192];
        snprintf(path, sizeof path, "%s/%s", root, g->node[i]);
        FILE *fp = fopen(path, "rb");
        if (!fp) continue;
        size_t len; char *src = read_file(fp, &len);
        fclose(fp);
        if (!src) continue;
        VLinks l = {0};
        vault_links(src, len, &l);
        for (int k = 0; k < l.n; k++) {
            int j = graph_find(g, l.v[k].target);
            if (j >= 0) edge_push(g, i, j, l.v[k].wiki);
        }
        vlinks_free(&l);
        free(src);
    }
    vfiles_free(&f);
}

void graph_free(Graph *g) {
    for (int i = 0; i < g->nn; i++) free(g->node[i]);
    free(g->node);
    free(g->edge);
    memset(g, 0, sizeof *g);
}
