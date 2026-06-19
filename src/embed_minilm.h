#ifndef GLANCE_EMBED_MINILM_H
#define GLANCE_EMBED_MINILM_H

#include "embed.h"

/* A real sentence-encoder Embedder backed by llama.cpp (an all-MiniLM-L6-v2
 * gguf), the semantic upgrade behind embed.h's seam. Compiled in only when
 * glance is built with semantic support (the GLANCE_SEMANTIC build flag links
 * the vendored libllama); the default build keeps the dependency-free hashing
 * embedder and never references this.
 *
 * embedder_minilm loads `model_path`, runs mean-pooled, L2-normalized
 * embeddings (dim is the model's, 384 for MiniLM-L6). `n_gpu_layers` is the
 * Metal offload count: 0 = CPU (no shader warm-up — the right default for the
 * one-shot CLI), 99 = offload to the GPU (worth it for a bulk .glance/ cache
 * build, where the one-time Metal compile amortizes). Returns NULL if the model
 * fails to load. Free with embedder_free (it tears down the model + context). */
Embedder *embedder_minilm(const char *model_path, int n_gpu_layers);

#endif /* GLANCE_EMBED_MINILM_H */
