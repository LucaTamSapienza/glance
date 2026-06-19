/* embcache.c — persistent content-addressed embedding cache. See embcache.h.
 *
 * On-disk format (`<dir>/.glance/embeddings.bin`, little-endian host order):
 *   magic   "GLEMB1\n"      (7 bytes)
 *   uint32  dim
 *   uint32  model_tag       (FNV-1a of model_id — changing the model invalidates)
 *   uint32  count
 *   count * { uint64 key ; float[dim] vec }
 *
 * In memory: keys/vecs grow together in insertion order (so saving is a straight
 * dump); a power-of-two open-addressing index maps key -> entry slot for O(1)
 * average lookup. Keys are a 64-bit FNV-1a of the section text; a collision
 * would mis-serve a vector, but at 64 bits over a vault's sections that is
 * negligible. */
#include "embcache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>

#define EMB_MAGIC "GLEMB1\n"
#define EMB_MAGIC_LEN 7

struct EmbCache {
    char    *path;       /* <dir>/.glance/embeddings.bin */
    char    *gdir;       /* <dir>/.glance */
    int      dim;
    uint32_t model_tag;

    uint64_t *keys;      /* count entries */
    float    *vecs;      /* count * dim floats */
    int       count, cap;

    int      *index;     /* icap slots, entry index or -1 */
    int       icap;

    int       dirty;
    int       hits, misses;
};

/* FNV-1a 64-bit over a byte span. */
static uint64_t fnv64(const char *s, size_t n) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++) { h ^= (unsigned char)s[i]; h *= 1099511628211ULL; }
    return h;
}

/* FNV-1a 32-bit over a NUL-terminated string (for the model tag). */
static uint32_t fnv32(const char *s) {
    uint32_t h = 2166136261u;
    for (; *s; s++) { h ^= (unsigned char)*s; h *= 16777619u; }
    return h;
}

/* Insert entry slot `ei` (its key already in keys[ei]) into the open-addressing
 * index. Caller guarantees spare capacity. */
static void index_insert(EmbCache *c, int ei) {
    uint64_t k = c->keys[ei];
    int mask = c->icap - 1;
    int b = (int)(k & (uint64_t)mask);
    while (c->index[b] != -1) b = (b + 1) & mask;
    c->index[b] = ei;
}

/* Grow + rebuild the index when the load factor would exceed ~0.7. */
static int index_reserve(EmbCache *c, int want) {
    if (c->icap >= want * 10 / 7 && c->icap > 0) return 0;
    int ncap = c->icap ? c->icap : 16;
    while (ncap * 7 / 10 < want) ncap <<= 1;
    int *ni = malloc((size_t)ncap * sizeof *ni);
    if (!ni) return -1;
    for (int i = 0; i < ncap; i++) ni[i] = -1;
    free(c->index);
    c->index = ni;
    c->icap = ncap;
    for (int e = 0; e < c->count; e++) index_insert(c, e);
    return 0;
}

/* Find the entry slot for key `k`, or -1. */
static int index_find(EmbCache *c, uint64_t k) {
    if (c->icap == 0) return -1;
    int mask = c->icap - 1;
    int b = (int)(k & (uint64_t)mask);
    while (c->index[b] != -1) {
        if (c->keys[c->index[b]] == k) return c->index[b];
        b = (b + 1) & mask;
    }
    return -1;
}

/* Append a (key, vec) pair to the arrays; returns the new slot or -1 on OOM. */
static int entries_push(EmbCache *c, uint64_t key, const float *vec) {
    if (c->count == c->cap) {
        int ncap = c->cap ? c->cap * 2 : 64;
        uint64_t *nk = realloc(c->keys, (size_t)ncap * sizeof *nk);
        if (!nk) return -1;
        c->keys = nk;
        float *nv = realloc(c->vecs, (size_t)ncap * (size_t)c->dim * sizeof *nv);
        if (!nv) return -1;
        c->vecs = nv;
        c->cap = ncap;
    }
    c->keys[c->count] = key;
    memcpy(c->vecs + (size_t)c->count * c->dim, vec, (size_t)c->dim * sizeof *vec);
    return c->count++;
}

/* Load an existing cache file into c (best-effort; a malformed or mismatched
 * file leaves c empty). */
static void load_file(EmbCache *c) {
    FILE *f = fopen(c->path, "rb");
    if (!f) return;
    char magic[EMB_MAGIC_LEN];
    uint32_t dim = 0, tag = 0, count = 0;
    if (fread(magic, 1, EMB_MAGIC_LEN, f) != EMB_MAGIC_LEN ||
        memcmp(magic, EMB_MAGIC, EMB_MAGIC_LEN) != 0 ||
        fread(&dim, sizeof dim, 1, f) != 1 ||
        fread(&tag, sizeof tag, 1, f) != 1 ||
        fread(&count, sizeof count, 1, f) != 1 ||
        (int)dim != c->dim || tag != c->model_tag) { fclose(f); return; }

    if (index_reserve(c, (int)count + 1) != 0) { fclose(f); return; }
    float *tmp = malloc((size_t)c->dim * sizeof *tmp);
    if (!tmp) { fclose(f); return; }
    for (uint32_t i = 0; i < count; i++) {
        uint64_t key;
        if (fread(&key, sizeof key, 1, f) != 1 ||
            fread(tmp, sizeof *tmp, (size_t)c->dim, f) != (size_t)c->dim) break;
        if (index_find(c, key) >= 0) continue;       /* dedupe defensively */
        int ei = entries_push(c, key, tmp);
        if (ei < 0) break;
        index_insert(c, ei);
    }
    free(tmp);
    fclose(f);
}

EmbCache *embcache_open(const char *dir, int dim, const char *model_id) {
    if (dim <= 0) return NULL;
    EmbCache *c = calloc(1, sizeof *c);
    if (!c) return NULL;
    c->dim = dim;
    c->model_tag = fnv32(model_id ? model_id : "");

    size_t dlen = strlen(dir);
    c->gdir = malloc(dlen + 9);
    c->path = malloc(dlen + 24);
    if (!c->gdir || !c->path) { embcache_free(c); return NULL; }
    snprintf(c->gdir, dlen + 9, "%s/.glance", dir);
    snprintf(c->path, dlen + 24, "%s/.glance/embeddings.bin", dir);

    load_file(c);
    return c;
}

const float *embcache_get(EmbCache *c, const char *text, size_t len) {
    uint64_t k = fnv64(text, len);
    int e = index_find(c, k);
    if (e < 0) { c->misses++; return NULL; }
    c->hits++;
    return c->vecs + (size_t)e * c->dim;
}

void embcache_put(EmbCache *c, const char *text, size_t len, const float *vec) {
    uint64_t k = fnv64(text, len);
    if (index_find(c, k) >= 0) return;
    if (index_reserve(c, c->count + 1) != 0) return;
    int ei = entries_push(c, k, vec);
    if (ei < 0) return;
    index_insert(c, ei);
    c->dirty = 1;
}

int embcache_save(EmbCache *c) {
    if (!c->dirty) return 0;
    mkdir(c->gdir, 0755);                 /* ok if it already exists */
    FILE *f = fopen(c->path, "wb");
    if (!f) return -1;
    uint32_t dim = (uint32_t)c->dim, tag = c->model_tag, count = (uint32_t)c->count;
    int ok = fwrite(EMB_MAGIC, 1, EMB_MAGIC_LEN, f) == EMB_MAGIC_LEN &&
             fwrite(&dim, sizeof dim, 1, f) == 1 &&
             fwrite(&tag, sizeof tag, 1, f) == 1 &&
             fwrite(&count, sizeof count, 1, f) == 1;
    for (int i = 0; ok && i < c->count; i++)
        ok = fwrite(&c->keys[i], sizeof c->keys[i], 1, f) == 1 &&
             fwrite(c->vecs + (size_t)i * c->dim, sizeof(float), (size_t)c->dim, f) == (size_t)c->dim;
    fclose(f);
    if (ok) c->dirty = 0;
    return ok ? 0 : -1;
}

void embcache_free(EmbCache *c) {
    if (!c) return;
    free(c->keys);
    free(c->vecs);
    free(c->index);
    free(c->path);
    free(c->gdir);
    free(c);
}

int embcache_hits(const EmbCache *c)   { return c ? c->hits : 0; }
int embcache_misses(const EmbCache *c) { return c ? c->misses : 0; }
