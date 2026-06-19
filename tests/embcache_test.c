/* embcache_test.c — unit tests for the persistent embedding cache. */
#include "../src/embcache.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static int fails = 0;
static void expect(int cond, const char *msg) {
    if (!cond) { printf("FAIL: %s\n", msg); fails++; }
}

static void fill(float *v, int dim, float base) {
    for (int i = 0; i < dim; i++) v[i] = base + (float)i * 0.001f;
}
static int veq(const float *a, const float *b, int dim) {
    for (int i = 0; i < dim; i++) if (a[i] != b[i]) return 0;
    return 1;
}

int main(void) {
    const int dim = 8;
    char tmpl[] = "/tmp/glance_embcache_XXXXXX";
    char *dir = mkdtemp(tmpl);
    expect(dir != NULL, "mkdtemp");

    float a[8], b[8], got[8];
    fill(a, dim, 1.0f);
    fill(b, dim, 2.0f);

    /* put + get within one session */
    EmbCache *c = embcache_open(dir, dim, "minilm-l6-384");
    expect(c != NULL, "open");
    expect(embcache_get(c, "alpha", 5) == NULL, "miss before put");
    embcache_put(c, "alpha", 5, a);
    embcache_put(c, "beta", 4, b);
    const float *ga = embcache_get(c, "alpha", 5);
    expect(ga && veq(ga, a, dim), "get alpha matches");
    const float *gb = embcache_get(c, "beta", 4);
    expect(gb && veq(gb, b, dim), "get beta matches");
    expect(embcache_get(c, "gamma", 5) == NULL, "miss on unknown");
    expect(embcache_save(c) == 0, "save");
    embcache_free(c);

    /* reopen: entries persist */
    c = embcache_open(dir, dim, "minilm-l6-384");
    const float *ra = embcache_get(c, "alpha", 5);
    expect(ra && veq(ra, a, dim), "alpha persisted");
    memcpy(got, embcache_get(c, "beta", 4), sizeof got);
    expect(veq(got, b, dim), "beta persisted");
    embcache_free(c);

    /* different model_id -> cache ignored (no stale cross-model vectors) */
    c = embcache_open(dir, dim, "other-model");
    expect(embcache_get(c, "alpha", 5) == NULL, "model change invalidates");
    embcache_free(c);

    /* different dim -> cache ignored */
    c = embcache_open(dir, dim + 1, "minilm-l6-384");
    expect(embcache_get(c, "alpha", 5) == NULL, "dim change invalidates");
    embcache_free(c);

    /* many entries: exercise index growth + lookup */
    c = embcache_open(dir, dim, "bulk");
    char key[32];
    for (int i = 0; i < 500; i++) {
        snprintf(key, sizeof key, "sec-%d", i);
        float v[8]; fill(v, dim, (float)i);
        embcache_put(c, key, strlen(key), v);
    }
    int ok = 1;
    for (int i = 0; i < 500; i++) {
        snprintf(key, sizeof key, "sec-%d", i);
        float v[8]; fill(v, dim, (float)i);
        const float *g = embcache_get(c, key, strlen(key));
        if (!g || !veq(g, v, dim)) { ok = 0; break; }
    }
    expect(ok, "500 entries all round-trip");
    expect(embcache_hits(c) >= 500, "hit counter");
    embcache_free(c);

    /* cleanup */
    char p[256];
    snprintf(p, sizeof p, "%s/.glance/embeddings.bin", dir); unlink(p);
    snprintf(p, sizeof p, "%s/.glance", dir); rmdir(p);
    rmdir(dir);

    if (fails == 0) printf("all embcache tests passed\n");
    return fails ? 1 : 0;
}
