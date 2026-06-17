/* agent.c — JSON exports of Markdown structure for tools and agents. */
#include "agent.h"
#include "render.h"
#include "toc.h"
#include "section.h"
#include "receipt.h"
#include "context.h"
#include "bm25.h"
#include "vault.h"
#include "graph.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>

/* Print s as a JSON string literal, escaping as needed. */
static void json_str(const char *s) {
    putchar('"');
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { putchar('\\'); putchar((char)c); }
        else if (c == '\n') fputs("\\n", stdout);
        else if (c == '\t') fputs("\\t", stdout);
        else if (c < 0x20) printf("\\u%04x", c);
        else putchar((char)c);
    }
    putchar('"');
}

void agent_outline(const char *src, size_t len) {
    Doc *d = render_doc(src, len, 100000, 1);   /* huge width: no wrapping */
    TOC t = {0};
    toc_build(d, &t);
    putchar('[');
    for (int i = 0; i < t.n; i++) {
        printf("%s{\"level\":%d,\"title\":", i ? "," : "", t.v[i].level);
        json_str(t.v[i].title);
        printf(",\"line\":%d}", t.v[i].line);
    }
    puts("]");
    toc_free(&t);
    doc_free(d);
}

void agent_section(const char *src, size_t len, const char *anchor) {
    Doc *d = render_doc(src, len, 100000, 1);   /* huge width: no wrapping */
    Section s = section_find(d, anchor);

    printf("{\"anchor\":");
    json_str(anchor ? anchor : "");
    printf(",\"found\":%s", s.found ? "true" : "false");

    if (s.found) {
        char *text = section_text(d, s.start, s.end);
        Receipt r = {
            text ? receipt_estimate_tokens(text, strlen(text)) : 0,
            receipt_estimate_tokens(src, len),
        };
        char rj[160];
        receipt_to_json(rj, sizeof rj, &r);
        printf(",\"level\":%d,\"start_line\":%d,\"end_line\":%d,\"text\":",
               s.level, s.start, s.end);
        json_str(text ? text : "");
        printf(",\"receipt\":%s", rj);
        free(text);
    }
    puts("}");
    doc_free(d);
}

void agent_links(const char *src, size_t len) {
    VLinks l = {0};
    vault_links(src, len, &l);
    putchar('[');
    for (int i = 0; i < l.n; i++) {
        printf("%s{\"target\":", i ? "," : "");
        json_str(l.v[i].target);
        printf(",\"wiki\":%s}", l.v[i].wiki ? "true" : "false");
    }
    puts("]");
    vlinks_free(&l);
}

/* ---- vault graph ---------------------------------------------------------- */

int agent_graph(const char *dir) {
    DIR *probe = opendir(dir);
    if (!probe) return 1;                          /* unreadable root */
    closedir(probe);

    Graph g;
    graph_build(dir, &g);                          /* shared with the TUI graph view */

    printf("{\"nodes\":[");
    for (int i = 0; i < g.nn; i++) { printf("%s", i ? "," : ""); json_str(g.node[i]); }
    printf("],\"edges\":[");
    for (int e = 0; e < g.ne; e++) {
        printf("%s{\"from\":", e ? "," : "");
        json_str(g.node[g.edge[e].from]); printf(",\"to\":");
        json_str(g.node[g.edge[e].to]); printf(",\"wiki\":%s}", g.edge[e].wiki ? "true" : "false");
    }
    puts("]}");
    graph_free(&g);
    return 0;
}

/* ---- context retrieval ---------------------------------------------------- */

/* One retrievable unit: a section of a note, with both projections precomputed. */
typedef struct {
    int    note;             /* file index (note_id for diversity) */
    char  *anchor;           /* heading text, or "" for the preamble (owned) */
    char  *full;             /* full section text (owned) */
    char  *abstract;         /* abstract projection (owned) */
    size_t full_tokens;
    size_t abstract_tokens;
    double score;            /* BM25 base, with the graph prior folded in */
} Sec;

/* Read dir/rel into a NUL-terminated buffer; *len gets the byte length, or NULL
 * on failure. */
static char *read_rel(const char *dir, const char *rel, size_t *len) {
    char path[4096];
    snprintf(path, sizeof path, "%s/%s", dir, rel);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *s = read_file(f, len);
    fclose(f);
    return s;
}

/* Append a section unit to a growable array. Takes ownership of anchor/full/abs. */
static void sec_push(Sec **v, int *n, int *cap, int note, char *anchor,
                     char *full, char *abstract) {
    if (*n == *cap) { *cap = *cap ? *cap * 2 : 64; *v = realloc(*v, (size_t)*cap * sizeof **v); }
    Sec *s = &(*v)[(*n)++];
    s->note = note;
    s->anchor = anchor;
    s->full = full;
    s->abstract = abstract;
    s->full_tokens = receipt_estimate_tokens(full, strlen(full));
    s->abstract_tokens = receipt_estimate_tokens(abstract, strlen(abstract));
    s->score = 0.0;
}

/* Strength of the graph prior relative to a section's own BM25 score. */
#define CTX_GRAPH_ALPHA 0.25

int agent_context(const char *dir, const char *query, size_t budget) {
    DIR *probe = opendir(dir);
    if (!probe) return 1;
    closedir(probe);

    VFiles files = {0};
    vault_scan(dir, &files);

    Sec   *sec = NULL;
    int    nsec = 0, cap = 0;
    size_t raw_tokens = 0;
    Bm25  *ix = bm25_new();

    /* Split every note into sections (preamble + each heading subtree) and index
     * each section's full text for BM25. */
    for (int k = 0; k < files.n; k++) {
        size_t len;
        char *src = read_rel(dir, files.v[k], &len);
        if (!src) continue;
        raw_tokens += receipt_estimate_tokens(src, len);

        Doc *d = render_doc(src, len, 100000, 1);
        TOC t = {0};
        toc_build(d, &t);

        int first_h = (t.n > 0) ? t.v[0].line : (int)d->nline;
        if (first_h > 0) {   /* preamble before the first heading */
            char *full = section_text(d, 0, first_h);
            char *abst = section_abstract(d, 0, first_h);
            int id = nsec;
            sec_push(&sec, &nsec, &cap, k, strdup(""), full, abst);
            bm25_add(ix, id, full, strlen(full));
        }
        for (int i = 0; i < t.n; i++) {
            int level = t.v[i].level, end = (int)d->nline;
            for (int j = i + 1; j < t.n; j++) if (t.v[j].level <= level) { end = t.v[j].line; break; }
            char *full = section_text(d, t.v[i].line, end);
            char *abst = section_abstract(d, t.v[i].line, end);
            int id = nsec;
            sec_push(&sec, &nsec, &cap, k, strdup(t.v[i].title), full, abst);
            bm25_add(ix, id, full, strlen(full));
        }
        toc_free(&t);
        doc_free(d);
        free(src);
    }
    bm25_finalize(ix);

    /* BM25 base scores. */
    Bm25Hit *hits = malloc((size_t)(nsec ? nsec : 1) * sizeof *hits);
    int nhit = bm25_search(ix, query, hits, nsec);
    for (int h = 0; h < nhit; h++)
        if (hits[h].id >= 0 && hits[h].id < nsec) sec[hits[h].id].score = hits[h].score;
    free(hits);

    /* Graph prior: a section is boosted by the best base score among sections in
     * its note's 1-hop neighbours, so notes linked to strong matches rank up. */
    Graph g;
    graph_build(dir, &g);
    double *file_best = calloc((size_t)(files.n ? files.n : 1), sizeof *file_best);
    int    *file2node = malloc((size_t)(files.n ? files.n : 1) * sizeof *file2node);
    int    *node2file = malloc((size_t)(g.nn ? g.nn : 1) * sizeof *node2file);
    for (int nidx = 0; nidx < g.nn; nidx++) node2file[nidx] = -1;
    for (int k = 0; k < files.n; k++) {
        file2node[k] = -1;
        for (int nidx = 0; nidx < g.nn; nidx++)
            if (!strcmp(g.node[nidx], files.v[k])) { file2node[k] = nidx; node2file[nidx] = k; break; }
    }
    for (int s = 0; s < nsec; s++)
        if (sec[s].score > file_best[sec[s].note]) file_best[sec[s].note] = sec[s].score;

    double *file_bonus = calloc((size_t)(files.n ? files.n : 1), sizeof *file_bonus);
    for (int k = 0; k < files.n; k++) {
        int nf = file2node[k];
        if (nf < 0) continue;
        double best = 0.0;
        for (int e = 0; e < g.ne; e++) {
            int other = -1;
            if (g.edge[e].from == nf) other = g.edge[e].to;
            else if (g.edge[e].to == nf) other = g.edge[e].from;
            if (other < 0) continue;
            int of = node2file[other];
            if (of >= 0 && file_best[of] > best) best = file_best[of];
        }
        file_bonus[k] = CTX_GRAPH_ALPHA * best;
    }
    for (int s = 0; s < nsec; s++)
        if (sec[s].score > 0.0) sec[s].score += file_bonus[sec[s].note];

    /* Candidates = sections with a positive score; plan under the budget. */
    CtxCand *cand = malloc((size_t)(nsec ? nsec : 1) * sizeof *cand);
    int    *cand2sec = malloc((size_t)(nsec ? nsec : 1) * sizeof *cand2sec);
    int     ncand = 0;
    for (int s = 0; s < nsec; s++) {
        if (sec[s].score <= 0.0) continue;
        cand2sec[ncand] = s;
        cand[ncand++] = (CtxCand){ sec[s].note, sec[s].score, sec[s].full_tokens, sec[s].abstract_tokens };
    }
    CtxPlan plan = context_plan(cand, ncand, budget);

    /* Emit the JSON bundle. */
    printf("{\"query\":");
    json_str(query);
    printf(",\"budget_tokens\":%zu,\"chunks\":[", budget);
    for (int i = 0; i < plan.npick; i++) {
        int s = cand2sec[plan.picks[i].cand];
        int abstract = (plan.picks[i].granularity == CTX_ABSTRACT);
        printf("%s{\"note\":", i ? "," : "");
        json_str(files.v[sec[s].note]);
        printf(",\"anchor\":");
        json_str(sec[s].anchor);
        printf(",\"granularity\":\"%s\",\"score\":%.3f,\"text\":",
               abstract ? "abstract" : "section", sec[s].score);
        json_str(abstract ? sec[s].abstract : sec[s].full);
        printf(",\"tokens\":%zu}", plan.picks[i].tokens);
    }
    printf("],\"truncated\":[");
    for (int i = 0; i < plan.ntrunc; i++) {
        int s = cand2sec[plan.trunc[i]];
        printf("%s{\"note\":", i ? "," : "");
        json_str(files.v[sec[s].note]);
        printf(",\"anchor\":");
        json_str(sec[s].anchor);
        printf(",\"score\":%.3f,\"tokens\":%zu}", sec[s].score, sec[s].full_tokens);
    }
    Receipt r = { plan.used_tokens, raw_tokens };
    char rj[160];
    receipt_to_json(rj, sizeof rj, &r);
    printf("],\"receipt\":%s}\n", rj);

    /* Cleanup. */
    context_plan_free(&plan);
    free(cand); free(cand2sec);
    free(file_best); free(file_bonus); free(file2node); free(node2file);
    graph_free(&g);
    for (int s = 0; s < nsec; s++) { free(sec[s].anchor); free(sec[s].full); free(sec[s].abstract); }
    free(sec);
    bm25_free(ix);
    vfiles_free(&files);
    return 0;
}
