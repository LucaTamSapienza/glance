/* embed_minilm_smoke.c — semantic sanity for the MiniLM Embedder. Needs the gguf
 * model (passed as argv[1] or $GLANCE_MINILM_MODEL); skips cleanly if absent, so
 * it never breaks a model-less CI. Not part of `make test` (it needs the vendored
 * llama build + a model); run via `make semantic-smoke MODEL=...`. */
#include "../src/embed_minilm.h"
#include "../src/embed.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    const char *model = argc > 1 ? argv[1] : getenv("GLANCE_MINILM_MODEL");
    if (!model) { printf("SKIP: no model (pass a path or set GLANCE_MINILM_MODEL)\n"); return 0; }
    int ngl = argc > 2 ? atoi(argv[2]) : 0;       /* CPU by default */

    Embedder *e = embedder_minilm(model, ngl);
    if (!e) { printf("FAIL: model load\n"); return 1; }
    int dim = embedder_dim(e);
    if (dim != 384) { printf("FAIL: dim = %d (expected 384)\n", dim); embedder_free(e); return 1; }

    float *a = malloc(sizeof(float) * dim);
    float *b = malloc(sizeof(float) * dim);
    float *c = malloc(sizeof(float) * dim);
    const char *ta = "atomic file saving and the kqueue file watcher";
    const char *tb = "saving files atomically with a filesystem watch";  /* related to ta */
    const char *tc = "a recipe for cooking tomato pasta with fresh basil"; /* unrelated */
    e->embed(e, ta, strlen(ta), a);
    e->embed(e, tb, strlen(tb), b);
    e->embed(e, tc, strlen(tc), c);

    float self = embed_cosine(a, a, dim);
    float rel  = embed_cosine(a, b, dim);
    float un   = embed_cosine(a, c, dim);
    printf("dim=%d  self=%.3f  related=%.3f  unrelated=%.3f\n", dim, self, rel, un);

    int ok = self > 0.99f && rel > un && rel > 0.30f;
    embedder_free(e);
    free(a); free(b); free(c);
    if (!ok) { printf("FAIL: expected self~1 and related>unrelated\n"); return 1; }
    printf("embed_minilm_smoke: passed\n");
    return 0;
}
