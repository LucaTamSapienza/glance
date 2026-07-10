/* doctor.c — vault hygiene facts (see doctor.h). */
#include "doctor.h"
#include "vault.h"
#include "graph.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
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

/* Count TODO / FIXME markers in `s`, skipping fenced code blocks (same fence
 * rules as edit.c: a run of >=3 backticks or tildes opens, a matching run at
 * least as long with no info string closes). A marker is the word immediately
 * followed by ':' or '(' — "TODO:", "TODO(seed):", "FIXME:" — so prose that
 * merely mentions TODO markers, or code quoted in a fence, doesn't count. */
static int count_todos(const char *s) {
    int n = 0, fence = 0, flen = 0;
    char fch = 0;
    for (const char *line = s; *line; ) {
        const char *nl = strchr(line, '\n');
        const char *end = nl ? nl : line + strlen(line);

        const char *t = line;
        while (t < end && t - line < 3 && *t == ' ') t++;
        if (t < end && (*t == '`' || *t == '~')) {
            const char *r = t;
            while (r < end && *r == *t) r++;
            if (r - t >= 3) {
                int info = 0;
                for (const char *k = r; k < end && !info; k++)
                    info = (*k != ' ' && *k != '\t');
                if (!fence) { fence = 1; fch = *t; flen = (int)(r - t); }
                else if (*t == fch && r - t >= flen && !info) fence = 0;
                line = nl ? nl + 1 : end;
                continue;
            }
        }
        if (!fence) {
            for (const char *p = line; p + 5 <= end; p++) {
                if (!memcmp(p, "TODO", 4) && (p[4] == ':' || p[4] == '(')) n++;
                else if (p + 6 <= end && !memcmp(p, "FIXME", 5) &&
                         (p[5] == ':' || p[5] == '(')) n++;
            }
        }
        line = nl ? nl + 1 : end;
    }
    return n;
}

/* True if link target `t` (as written, possibly "note#heading") resolves to
 * something in the vault: a note found by stem, or — when the target carries
 * an extension, e.g. an image embed — a file relative to the linking note's
 * directory or to the vault root. */
static int target_resolves(const char *root, const char *note, const char *t) {
    char clean[512];
    snprintf(clean, sizeof clean, "%s", t);
    char *hash = strchr(clean, '#');
    if (hash) *hash = '\0';              /* [[note#heading]] addresses the note */
    if (!clean[0]) return 1;             /* a pure in-page anchor */
    char *hit = vault_find(root, clean);
    if (hit) { free(hit); return 1; }
    if (!strchr(clean, '.')) return 0;   /* bare note name: the stem search decided */
    struct stat st;
    char path[4096];
    const char *slash = strrchr(note, '/');
    if (slash) {                         /* relative to the note's directory */
        snprintf(path, sizeof path, "%s/%.*s/%s",
                 root, (int)(slash - note), note, clean);
        if (stat(path, &st) == 0) return 1;
    }
    snprintf(path, sizeof path, "%s/%s", root, clean);   /* or to the root */
    return stat(path, &st) == 0;
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

/* Deterministic report order: sort by note path. */
static int note_cmp(const void *a, const void *b) {
    return strcmp(((const DoctorNote *)a)->note, ((const DoctorNote *)b)->note);
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
        if (!src) { dn->unreadable = 1; continue; }
        dn->lines = count_lines(src);
        dn->todos = count_todos(src);

        /* Dangling links: extracted here rather than from the graph, because
         * the graph only records links that resolved. Every [[wikilink]] is
         * checked; a Markdown link only when it is a vault-relative .md path
         * (URLs, mailto:, pure anchors and non-note assets are not ours). */
        VLinks links = {0};
        vault_links(src, len, &links);
        for (int l = 0; l < links.n; l++) {
            const char *t = links.v[l].target;
            if (!links.v[l].wiki) {
                if (strchr(t, ':') || t[0] == '#') continue;
                char base[512];
                snprintf(base, sizeof base, "%s", t);
                char *h = strchr(base, '#');
                if (h) *h = '\0';
                size_t bn = strlen(base);
                if (bn < 4 || strcasecmp(base + bn - 3, ".md") != 0) continue;
            }
            if (!target_resolves(root, g.node[i], t)) push_dangling(dn, t);
        }
        vlinks_free(&links);
        free(src);
    }

    for (int e = 0; e < g.ne; e++) {
        if (g.edge[e].from == g.edge[e].to) continue;   /* self-links say nothing */
        int dup = 0;                       /* degree counts distinct pairs once */
        for (int p = 0; p < e && !dup; p++)
            dup = g.edge[p].from == g.edge[e].from && g.edge[p].to == g.edge[e].to;
        if (dup) continue;
        out->v[g.edge[e].from].outbound++;
        out->v[g.edge[e].to].inbound++;
    }

    qsort(out->v, (size_t)out->n, sizeof *out->v, note_cmp);
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
