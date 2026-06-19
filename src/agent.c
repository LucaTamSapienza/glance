/* agent.c — JSON exports of Markdown structure for tools and agents. */
#include "agent.h"
#include "render.h"
#include "toc.h"
#include "section.h"
#include "receipt.h"
#include "context.h"
#include "bm25.h"
#include "embed.h"
#include "embcache.h"
#ifdef GLANCE_SEMANTIC
#include "embed_minilm.h"
#endif
#include "edit.h"
#include "fs_save.h"
#include "vault.h"
#include "graph.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>

/* Read dir/rel into a NUL-terminated buffer; *len gets the byte length, or NULL
 * on failure. Shared by the vault-scanning exports below. */
static char *read_rel(const char *dir, const char *rel, size_t *len) {
    char path[4096];
    snprintf(path, sizeof path, "%s/%s", dir, rel);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *s = read_file(f, len);
    fclose(f);
    return s;
}

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

void agent_outline_ex(const char *src, size_t len, int depth, int abstract) {
    Doc *d = render_doc(src, len, 100000, 1);   /* huge width: no wrapping */
    TOC t = {0};
    toc_build(d, &t);
    putchar('[');
    int first = 1;
    for (int i = 0; i < t.n; i++) {
        if (depth > 0 && t.v[i].level > depth) continue;   /* bounded depth */
        printf("%s{\"level\":%d,\"title\":", first ? "" : ",", t.v[i].level);
        json_str(t.v[i].title);
        printf(",\"line\":%d", t.v[i].line);
        if (abstract) {
            int level = t.v[i].level, end = (int)d->nline;
            for (int j = i + 1; j < t.n; j++) if (t.v[j].level <= level) { end = t.v[j].line; break; }
            char *ab = section_abstract(d, t.v[i].line, end);
            char *body = ab;   /* drop the heading line (already in "title") */
            if (ab) { char *nl = strchr(ab, '\n'); if (nl) body = nl + 1; }
            while (body && (*body == '\n' || *body == ' ' || *body == '\t')) body++;
            if (body) {        /* trim trailing whitespace for a clean field */
                size_t bl = strlen(body);
                while (bl > 0 && (body[bl-1] == '\n' || body[bl-1] == ' ' ||
                                  body[bl-1] == '\t' || body[bl-1] == '\r')) body[--bl] = '\0';
            }
            printf(",\"abstract\":");
            json_str(body ? body : "");
            free(ab);
        }
        putchar('}');
        first = 0;
    }
    puts("]");
    toc_free(&t);
    doc_free(d);
}

void agent_outline(const char *src, size_t len) { agent_outline_ex(src, len, 0, 0); }

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

/* ---- graph neighbourhood -------------------------------------------------- */

int agent_neighbors(const char *dir, const char *note, int depth) {
    DIR *probe = opendir(dir);
    if (!probe) return 1;
    closedir(probe);
    if (depth <= 0) depth = 1;

    Graph g;
    graph_build(dir, &g);
    int start = graph_find(&g, note);

    printf("{\"note\":");
    json_str(note);
    if (start < 0) { puts(",\"found\":false,\"neighbors\":[]}"); graph_free(&g); return 0; }
    printf(",\"found\":true,\"depth\":%d,\"neighbors\":[", depth);

    int n = g.nn ? g.nn : 1;
    int *dist    = malloc((size_t)n * sizeof(int));
    int *linkdir = calloc((size_t)n, sizeof(int));   /* 1=outbound 2=backlink 3=both 0=path */
    int *q       = malloc((size_t)n * sizeof(int));
    for (int i = 0; i < g.nn; i++) dist[i] = -1;

    /* Breadth-first to `depth` hops over the undirected link graph; record the
     * link direction only for the immediate (distance-1) neighbours. */
    int qh = 0, qt = 0;
    dist[start] = 0;
    q[qt++] = start;
    while (qh < qt) {
        int u = q[qh++];
        if (dist[u] >= depth) continue;
        for (int e = 0; e < g.ne; e++) {
            int v = -1, outbound = 0;
            if (g.edge[e].from == u)      { v = g.edge[e].to;   outbound = 1; }
            else if (g.edge[e].to == u)   { v = g.edge[e].from; outbound = 0; }
            if (v < 0 || v == start) continue;
            if (dist[v] == -1) { dist[v] = dist[u] + 1; q[qt++] = v; }
            if (u == start) linkdir[v] |= outbound ? 1 : 2;
        }
    }

    int first = 1;
    for (int v = 0; v < g.nn; v++) {
        if (v == start || dist[v] < 0) continue;
        const char *d = linkdir[v] == 3 ? "both" : linkdir[v] == 1 ? "outbound"
                      : linkdir[v] == 2 ? "backlink" : "path";
        printf("%s{\"note\":", first ? "" : ",");
        json_str(g.node[v]);
        printf(",\"distance\":%d,\"direction\":\"%s\"}", dist[v], d);
        first = 0;
    }
    puts("]}");

    free(dist); free(linkdir); free(q);
    graph_free(&g);
    return 0;
}

/* ---- backlinks & recency -------------------------------------------------- */

/* ASCII case-insensitive string equality. */
static int ci_eq(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        unsigned char ca = (unsigned char)*a, cb = (unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0;
    }
    return *a == *b;
}

/* The first source line of `src` that mentions `needle` (case-insensitive),
 * trimmed of surrounding whitespace, copied into out[cap]; out is emptied if no
 * line matches. This is the "why it links here" snippet for a backlink. */
static void mention_line(const char *src, const char *needle, char *out, size_t cap) {
    out[0] = '\0';
    size_t nl = strlen(needle);
    if (nl == 0) return;
    const char *line = src;
    while (*line) {
        const char *eol = strchr(line, '\n');
        size_t llen = eol ? (size_t)(eol - line) : strlen(line);
        for (size_t i = 0; i + nl <= llen; i++) {
            if (strncasecmp(line + i, needle, nl) == 0) {
                const char *s = line; size_t e = llen;
                while (s < line + e && (*s == ' ' || *s == '\t')) s++;
                while (e > (size_t)(s - line) && (line[e-1] == ' ' || line[e-1] == '\t' || line[e-1] == '\r')) e--;
                size_t len = (size_t)(line + e - s);
                if (len >= cap) len = cap - 1;
                memcpy(out, s, len); out[len] = '\0';
                return;
            }
        }
        if (!eol) break;
        line = eol + 1;
    }
}

int agent_backlinks(const char *dir, const char *note, int want_context) {
    DIR *probe = opendir(dir);
    if (!probe) return 1;
    closedir(probe);

    char target[512];
    vault_stem(note, target, sizeof target);

    VFiles files = {0};
    vault_scan(dir, &files);

    printf("{\"note\":");
    json_str(note);
    printf(",\"backlinks\":[");
    int first = 1;
    for (int k = 0; k < files.n; k++) {
        size_t len;
        char *src = read_rel(dir, files.v[k], &len);
        if (!src) continue;

        VLinks l = {0};
        vault_links(src, len, &l);
        int linked = 0;
        for (int i = 0; i < l.n && !linked; i++) {
            char stem[512];
            vault_stem(l.v[i].target, stem, sizeof stem);
            if (ci_eq(stem, target)) linked = 1;
        }
        vlinks_free(&l);

        if (linked) {
            printf("%s{\"note\":", first ? "" : ",");
            json_str(files.v[k]);
            if (want_context) {
                char snippet[512];
                mention_line(src, target, snippet, sizeof snippet);
                printf(",\"context\":");
                json_str(snippet);
            }
            putchar('}');
            first = 0;
        }
        free(src);
    }
    puts("]}");
    vfiles_free(&files);
    return 0;
}

int agent_since(const char *dir, long since) {
    DIR *probe = opendir(dir);
    if (!probe) return 1;
    closedir(probe);

    VFiles files = {0};
    vault_scan(dir, &files);

    printf("{\"since\":%ld,\"changed\":[", since);
    int first = 1;
    for (int k = 0; k < files.n; k++) {
        char path[4096];
        snprintf(path, sizeof path, "%s/%s", dir, files.v[k]);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if ((long)st.st_mtime <= since) continue;
        printf("%s{\"note\":", first ? "" : ",");
        json_str(files.v[k]);
        printf(",\"mtime\":%ld}", (long)st.st_mtime);
        first = 0;
    }
    puts("]}");
    vfiles_free(&files);
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
/* Embedding dimension and the weight of the semantic signal in the fused score. */
#define CTX_EMBED_DIM      256
#define CTX_SEMANTIC_LAMBDA 1.0

/* Resolve the embedder for --semantic and tag it for the on-disk cache. With a
 * MiniLM build (GLANCE_SEMANTIC) and a model available, use the real sentence
 * encoder (CPU by default — no Metal shader warm-up on the one-shot CLI path;
 * GLANCE_MINILM_NGL overrides); otherwise the dependency-free hashing embedder.
 * minilm_model_path resolves the gguf ($GLANCE_MINILM_MODEL, else the cached
 * default, downloaded on first use). *model_id is a short stable string so the
 * cache never mixes vectors from different encoders. Returns NULL on OOM. */
static Embedder *resolve_embedder(const char **model_id) {
#ifdef GLANCE_SEMANTIC
    const char *path = minilm_model_path();           /* env -> cache -> download */
    if (path) {
        const char *g = getenv("GLANCE_MINILM_NGL");
        Embedder *e = embedder_minilm(path, g ? atoi(g) : 0);
        if (e) { *model_id = "minilm-l6-384"; return e; }
    }
#endif
    *model_id = "hash-256";
    return embedder_default(CTX_EMBED_DIM);
}

int agent_context(const char *dir, const char *query, size_t budget, int semantic) {
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

    /* Optional semantic fusion: blend the lexical score (normalized to [0,1] by
     * the top BM25 score) with each section's embedding cosine to the query, so
     * notes a keyword search misses can still surface. Lexical-only is default. */
    if (semantic) {
        const char *model_id = NULL;
        Embedder *emb = resolve_embedder(&model_id);
        if (emb) {
            int dim = emb->dim;
            /* Cache section vectors under <dir>/.glance/: only the query is
             * embedded live, unchanged sections hit the cache, edited ones miss
             * and are re-embedded. Keyed by the model + section text. */
            EmbCache *cache = embcache_open(dir, dim, model_id);
            float *qv = malloc((size_t)dim * sizeof *qv);
            float *sv = malloc((size_t)dim * sizeof *sv);
            if (qv && sv) {
                emb->embed(emb, query, strlen(query), qv);
                double maxb = 0.0;
                for (int s = 0; s < nsec; s++) if (sec[s].score > maxb) maxb = sec[s].score;
                for (int s = 0; s < nsec; s++) {
                    double bn = (maxb > 0.0) ? sec[s].score / maxb : 0.0;
                    const char *txt = sec[s].full;
                    size_t tl = strlen(txt);
                    const float *vec = cache ? embcache_get(cache, txt, tl) : NULL;
                    if (!vec) {                       /* miss: embed now, remember it */
                        emb->embed(emb, txt, tl, sv);
                        vec = sv;
                        if (cache) embcache_put(cache, txt, tl, sv);
                    }
                    double cos = embed_cosine(qv, vec, dim);
                    if (cos < 0.0) cos = 0.0;
                    sec[s].score = bn + CTX_SEMANTIC_LAMBDA * cos;
                }
            }
            free(qv); free(sv);
            if (cache) { embcache_save(cache); embcache_free(cache); }
            embedder_free(emb);
        }
    }

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
    printf(",\"budget_tokens\":%zu,\"semantic\":%s,\"chunks\":[",
           budget, semantic ? "true" : "false");
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

/* ---- surgical writes ------------------------------------------------------ */

/* Read a whole file by path into a NUL-terminated buffer; *len gets the length. */
static char *read_path(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *s = read_file(f, len);
    fclose(f);
    return s;
}

int agent_edit(const char *file, const char *anchor, int op, const char *text) {
    size_t len;
    char *src = read_path(file, &len);
    if (!src) { puts("{\"ok\":false,\"error\":\"cannot read file\"}"); return 1; }

    char *out = edit_section(src, len, anchor, (EditOp)op, text ? text : "");
    free(src);
    if (!out) { puts("{\"ok\":false,\"error\":\"heading not found\"}"); return 1; }

    if (atomic_write(file, out, strlen(out)) != 0) {
        puts("{\"ok\":false,\"error\":\"write failed\"}"); free(out); return 1;
    }

    /* Confirm by echoing the updated section back. */
    Doc *d = render_doc(out, strlen(out), 100000, 1);
    Section s = section_find(d, anchor);
    char *sectext = s.found ? section_text(d, s.start, s.end) : NULL;
    printf("{\"ok\":true,\"file\":");
    json_str(file);
    printf(",\"anchor\":");
    json_str(anchor ? anchor : "");
    printf(",\"bytes\":%zu,\"section\":", strlen(out));
    json_str(sectext ? sectext : "");
    puts("}");
    free(sectext); doc_free(d); free(out);
    return 0;
}

int agent_frontmatter(const char *file, const char *key, const char *value) {
    size_t len;
    char *src = read_path(file, &len);
    if (!src) { puts("{\"ok\":false,\"error\":\"cannot read file\"}"); return 1; }

    char *out = edit_frontmatter(src, len, key, value);
    free(src);
    if (!out) { puts("{\"ok\":false,\"error\":\"out of memory\"}"); return 1; }

    if (atomic_write(file, out, strlen(out)) != 0) {
        puts("{\"ok\":false,\"error\":\"write failed\"}"); free(out); return 1;
    }
    printf("{\"ok\":true,\"file\":");
    json_str(file);
    printf(",\"key\":");
    json_str(key);
    printf(",\"value\":");
    json_str(value);
    printf(",\"bytes\":%zu}\n", strlen(out));
    free(out);
    return 0;
}
