/* embed_minilm.c — a sentence-encoder Embedder backed by llama.cpp. See
 * embed_minilm.h. Built only under GLANCE_SEMANTIC (the Makefile links the
 * vendored libllama there); the rest of glance never references it, so the
 * default build carries no llama dependency.
 *
 * The model is a BERT-class encoder (all-MiniLM-L6-v2): one llama_context with
 * embeddings + MEAN pooling. Each embed() tokenizes the text, clears the prior
 * sequence from memory, decodes the batch as sequence 0, reads the pooled vector
 * (llama_get_embeddings_seq), and L2-normalizes it into the caller's buffer.
 */
#include "embed_minilm.h"

#include "llama.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

/* Per-embedder model state, hung off Embedder.ctx. */
typedef struct {
    struct llama_model       *model;
    struct llama_context     *ctx;
    const struct llama_vocab *vocab;
} MiniLM;

/* Drop every llama log line — glance speaks JSON on stdout and must stay quiet on
 * stderr for the agent path. */
static void minilm_quiet_log(enum ggml_log_level level, const char *text, void *user) {
    (void)level; (void)text; (void)user;
}

/* Embed one string into `out` (dim floats, L2-normalized; zero vector on any
 * failure or empty input). */
static void minilm_embed(const Embedder *e, const char *text, size_t len, float *out) {
    MiniLM *m = (MiniLM *)e->ctx;
    int dim = e->dim;
    memset(out, 0, (size_t)dim * sizeof *out);
    if (len == 0) return;

    int cap = (int)len + 8;
    llama_token *toks = malloc(sizeof *toks * (size_t)cap);
    if (!toks) return;
    int n = llama_tokenize(m->vocab, text, (int)len, toks, cap, true, false);
    if (n < 0) {                                  /* buffer too small: retry at the exact size */
        cap = -n;
        llama_token *bigger = realloc(toks, sizeof *toks * (size_t)cap);
        if (!bigger) { free(toks); return; }
        toks = bigger;
        n = llama_tokenize(m->vocab, text, (int)len, toks, cap, true, false);
    }
    if (n <= 0) { free(toks); return; }

    int n_ctx = (int)llama_n_ctx(m->ctx);         /* truncate to context, like sentence-transformers */
    if (n > n_ctx) n = n_ctx;

    llama_memory_clear(llama_get_memory(m->ctx), true);   /* sequences are independent */

    struct llama_batch batch = llama_batch_init(n, 0, 1);
    for (int i = 0; i < n; i++) {
        batch.token[i]    = toks[i];
        batch.pos[i]      = i;
        batch.n_seq_id[i] = 1;
        batch.seq_id[i][0] = 0;
        batch.logits[i]   = true;                 /* output every token for mean pooling */
    }
    batch.n_tokens = n;

    if (llama_decode(m->ctx, batch) >= 0) {
        const float *emb = llama_get_embeddings_seq(m->ctx, 0);
        if (emb) {
            double norm = 0.0;
            for (int i = 0; i < dim; i++) norm += (double)emb[i] * emb[i];
            norm = sqrt(norm);
            if (norm > 0.0) for (int i = 0; i < dim; i++) out[i] = (float)(emb[i] / norm);
        }
    }

    llama_batch_free(batch);
    free(toks);
}

/* Tear down the model + context behind Embedder.ctx (called by embedder_free). */
static void minilm_free(void *ctx) {
    MiniLM *m = (MiniLM *)ctx;
    if (!m) return;
    if (m->ctx)   llama_free(m->ctx);
    if (m->model) llama_model_free(m->model);
    free(m);
}

/* See embed_minilm.h. */
Embedder *embedder_minilm(const char *model_path, int n_gpu_layers) {
    static int backend_inited = 0;
    if (!backend_inited) {                        /* global, once per process */
        llama_log_set(minilm_quiet_log, NULL);
        llama_backend_init();
        backend_inited = 1;
    }

    struct llama_model_params mp = llama_model_default_params();
    mp.n_gpu_layers = n_gpu_layers;
    struct llama_model *model = llama_model_load_from_file(model_path, mp);
    if (!model) return NULL;

    struct llama_context_params cp = llama_context_default_params();
    cp.n_ctx        = 512;
    cp.n_batch      = 512;
    cp.embeddings   = true;
    cp.pooling_type = LLAMA_POOLING_TYPE_MEAN;
    const char *th = getenv("GLANCE_MINILM_THREADS");   /* default: llama's auto */
    if (th && atoi(th) > 0) { cp.n_threads = atoi(th); cp.n_threads_batch = atoi(th); }
    struct llama_context *lctx = llama_init_from_model(model, cp);
    if (!lctx) { llama_model_free(model); return NULL; }

    MiniLM *m = calloc(1, sizeof *m);
    if (!m) { llama_free(lctx); llama_model_free(model); return NULL; }
    m->model = model;
    m->ctx   = lctx;
    m->vocab = llama_model_get_vocab(model);

    Embedder *e = calloc(1, sizeof *e);
    if (!e) { minilm_free(m); return NULL; }
    e->dim      = llama_model_n_embd(model);
    e->embed    = minilm_embed;
    e->ctx      = m;
    e->free_ctx = minilm_free;
    return e;
}

/* ---- model resolution + first-use download --------------------------------- */

#define MINILM_FILE "all-MiniLM-L6-v2-ggml-model-f16.gguf"
#define MINILM_URL  "https://huggingface.co/second-state/All-MiniLM-L6-v2-Embedding-GGUF/resolve/main/" MINILM_FILE

/* True if `path` is a non-empty file whose first bytes are the GGUF magic. */
static int is_gguf(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0 || st.st_size <= 0) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char m[4] = {0};
    size_t got = fread(m, 1, 4, f);
    fclose(f);
    return got == 4 && memcmp(m, "GGUF", 4) == 0;
}

/* Run `curl -fL -o out url` without a shell (no injection from $HOME). Returns 0
 * on a clean exit. */
static int fetch(const char *url, const char *out) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        execlp("curl", "curl", "-fL", "--retry", "2", "-o", out, url, (char *)NULL);
        _exit(127);                               /* curl not found */
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return -1;
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
}

/* See embed_minilm.h. */
const char *minilm_model_path(void) {
    static char path[4096];

    const char *env = getenv("GLANCE_MINILM_MODEL");
    if (env && *env && is_gguf(env)) { snprintf(path, sizeof path, "%s", env); return path; }

    const char *home = getenv("HOME");
    if (!home || !*home) return NULL;
    char dir[3500];
    snprintf(dir, sizeof dir, "%s/.cache/glance", home);
    snprintf(path, sizeof path, "%s/%s", dir, MINILM_FILE);
    if (is_gguf(path)) return path;

    /* First use: download to the cache dir. mkdir the chain by hand (no shell). */
    char cache[3500];
    snprintf(cache, sizeof cache, "%s/.cache", home);
    mkdir(cache, 0755);
    mkdir(dir, 0755);

    fprintf(stderr, "glance: fetching the all-MiniLM-L6-v2 model (~44 MB, one-time) -> %s\n", path);
    char tmp[4096];
    snprintf(tmp, sizeof tmp, "%s.part", path);
    if (fetch(MINILM_URL, tmp) != 0 || !is_gguf(tmp)) {
        unlink(tmp);
        fprintf(stderr, "glance: model download failed; using the lexical fallback.\n");
        return NULL;
    }
    if (rename(tmp, path) != 0) { unlink(tmp); return NULL; }
    fprintf(stderr, "glance: model ready.\n");
    return path;
}
