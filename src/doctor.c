/* doctor.c — vault hygiene facts (see doctor.h). */
#include "doctor.h"
#include "vault.h"
#include "graph.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

/* Read root/rel into a NUL-terminated buffer; NULL on failure. */
static char *read_note(const char *root, const char *rel, size_t *len) {
    char path[4096];
    snprintf(path, sizeof path, "%s/%s", root, rel);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *s = read_file(f, len);
    fclose(f);
    return s;
}

/* Count the source lines of `s`: newline count, plus one for a trailing
 * unterminated line. An empty note has 0 lines. */
static int count_lines(const char *s) {
    int n = 0, tail = 0;
    for (; *s; s++) {
        if (*s == '\n') { n++; tail = 0; }
        else tail = 1;
    }
    return n + tail;
}

/* Count TODO / FIXME markers in `s`. */
static int count_todos(const char *s) {
    int n = 0;
    for (const char *p = s; (p = strstr(p, "TODO")) != NULL; p += 4) n++;
    for (const char *p = s; (p = strstr(p, "FIXME")) != NULL; p += 5) n++;
    return n;
}

/* Append `name` to the note's dangling list unless already recorded. */
static void push_dangling(DoctorNote *dn, const char *name) {
    for (int i = 0; i < dn->ndangling; i++)
        if (!strcmp(dn->dangling[i], name)) return;
    char **grown = realloc(dn->dangling, (size_t)(dn->ndangling + 1) * sizeof *grown);
    if (!grown) return;
    dn->dangling = grown;
    dn->dangling[dn->ndangling] = strdup(name);
    if (dn->dangling[dn->ndangling]) dn->ndangling++;
}

int doctor_scan(const char *root, DoctorReport *out) {
    out->v = NULL;
    out->n = 0;
    DIR *probe = opendir(root);
    if (!probe) return 1;
    closedir(probe);

    /* The graph's nodes are the vault scan; its edges are the resolved links,
     * so inbound/outbound counts fall straight out of it. */
    Graph g;
    graph_build(root, &g);
    if (g.nn == 0) { graph_free(&g); return 0; }

    out->v = calloc((size_t)g.nn, sizeof *out->v);
    if (!out->v) { graph_free(&g); return 1; }
    out->n = g.nn;

    for (int i = 0; i < g.nn; i++) {
        DoctorNote *dn = &out->v[i];
        dn->note = strdup(g.node[i]);

        char path[4096];
        snprintf(path, sizeof path, "%s/%s", root, g.node[i]);
        struct stat st;
        dn->mtime = (stat(path, &st) == 0) ? (long)st.st_mtime : 0;

        size_t len = 0;
        char *src = read_note(root, g.node[i], &len);
        if (!src) continue;
        dn->lines = count_lines(src);
        dn->todos = count_todos(src);

        /* Dangling [[wikilinks]]: extracted here rather than from the graph,
         * because the graph only records links that resolved to a file. */
        VLinks links = {0};
        vault_links(src, len, &links);
        for (int l = 0; l < links.n; l++) {
            if (!links.v[l].wiki) continue;
            char *hit = vault_find(root, links.v[l].target);
            if (hit) free(hit);
            else push_dangling(dn, links.v[l].target);
        }
        vlinks_free(&links);
        free(src);
    }

    for (int e = 0; e < g.ne; e++) {
        if (g.edge[e].from == g.edge[e].to) continue;   /* self-links say nothing */
        out->v[g.edge[e].from].outbound++;
        out->v[g.edge[e].to].inbound++;
    }

    graph_free(&g);
    return 0;
}

void doctor_free(DoctorReport *out) {
    for (int i = 0; i < out->n; i++) {
        free(out->v[i].note);
        for (int d = 0; d < out->v[i].ndangling; d++) free(out->v[i].dangling[d]);
        free(out->v[i].dangling);
    }
    free(out->v);
    out->v = NULL;
    out->n = 0;
}
