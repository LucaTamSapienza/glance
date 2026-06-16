/* graph_test.c — unit tests for the vault link graph, over a temp tree. */
#include "../src/graph.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static int fails = 0;
static void expect(int cond, const char *msg) {
    if (!cond) { printf("FAIL: %s\n", msg); fails++; }
}

static void writef(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    if (f) { fputs(content, f); fclose(f); }
}

/* Count edges from node `f` to node `t`. */
static int edges_between(const Graph *g, int from, int to) {
    int n = 0;
    for (int e = 0; e < g->ne; e++)
        if (g->edge[e].from == from && g->edge[e].to == to) n++;
    return n;
}

int main(void) {
    char dir[] = "/tmp/glance_graph_XXXXXX";
    if (!mkdtemp(dir)) { printf("mkdtemp failed\n"); return 1; }
    char p[1024], sub[1024];
    snprintf(sub, sizeof sub, "%s/notes", dir); mkdir(sub, 0755);
    snprintf(p, sizeof p, "%s/index.md", dir);          writef(p, "see [[Deep]] and [[index]]\n");
    snprintf(p, sizeof p, "%s/notes/Deep.md", dir);     writef(p, "back to [[index]]\n");

    Graph g;
    graph_build(dir, &g);
    expect(g.nn == 2, "two nodes across subdir");

    int idx = graph_find(&g, "index");
    int deep = graph_find(&g, "Deep");
    expect(idx >= 0 && deep >= 0, "both nodes findable by stem");
    expect(graph_find(&g, "missing") == -1, "missing stem -> -1");

    if (idx >= 0 && deep >= 0) {
        expect(edges_between(&g, idx, deep) == 1, "index -> Deep edge");
        expect(edges_between(&g, deep, idx) == 1, "Deep -> index edge");
        expect(edges_between(&g, idx, idx) == 1, "self-link counts as an edge");
    }
    graph_free(&g);

    snprintf(p, sizeof p, "%s/index.md", dir);      unlink(p);
    snprintf(p, sizeof p, "%s/notes/Deep.md", dir); unlink(p);
    rmdir(sub); rmdir(dir);

    if (fails) { printf("%d graph test(s) FAILED\n", fails); return 1; }
    printf("all graph tests passed\n");
    return 0;
}
