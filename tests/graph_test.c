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

    /* graph_expand: a chain A -> B -> C. Seed only A; B is 1 hop away, C is 2.
     * With khop=1 the activation reaches B but not C; with khop=2 it surfaces C
     * (the zero-lexical neighbour), and B still outranks C. */
    snprintf(p, sizeof p, "%s/A.md", dir); writef(p, "see [[B]]\n");
    snprintf(p, sizeof p, "%s/B.md", dir); writef(p, "see [[C]]\n");
    snprintf(p, sizeof p, "%s/C.md", dir); writef(p, "leaf\n");
    Graph g2;
    graph_build(dir, &g2);
    int a = graph_find(&g2, "A"), b = graph_find(&g2, "B"), c = graph_find(&g2, "C");
    expect(a >= 0 && b >= 0 && c >= 0, "chain nodes findable");
    if (a >= 0 && b >= 0 && c >= 0) {
        double *seed = calloc((size_t)g2.nn, sizeof *seed);
        double *out  = malloc((size_t)g2.nn * sizeof *out);
        seed[a] = 1.0;

        graph_expand(&g2, seed, 1, 0.5, out);
        expect(out[b] > 0.0, "1-hop activates B");
        expect(out[c] == 0.0, "1-hop does not reach C");

        graph_expand(&g2, seed, 2, 0.5, out);
        expect(out[c] > 0.0, "2-hop surfaces zero-lexical C");
        expect(out[b] > out[c], "nearer neighbour outranks farther one");

        graph_expand(&g2, seed, 0, 0.5, out);
        expect(out[b] == 0.0 && out[c] == 0.0, "khop=0 yields no expansion");

        free(seed); free(out);
    }
    graph_free(&g2);
    snprintf(p, sizeof p, "%s/A.md", dir); unlink(p);
    snprintf(p, sizeof p, "%s/B.md", dir); unlink(p);
    snprintf(p, sizeof p, "%s/C.md", dir); unlink(p);

    rmdir(sub); rmdir(dir);

    if (fails) { printf("%d graph test(s) FAILED\n", fails); return 1; }
    printf("all graph tests passed\n");
    return 0;
}
