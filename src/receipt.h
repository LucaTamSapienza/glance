/* receipt.h — pure logic for a "token receipt": how many LLM tokens a piece of
 * text costs, and how much was saved versus a naive whole-file read.
 *
 * No tokenizer, no network, no global state: just a deterministic estimate and
 * a couple of bounded formatters. The TUI/agent wiring lives elsewhere; the
 * parts that can be reasoned about without a model live here so they can be
 * unit tested. */
#ifndef RECEIPT_H
#define RECEIPT_H

#include <stddef.h>

/* Estimate the number of LLM tokens in `len` bytes of UTF-8 `text`.
   v1 heuristic (documented in the .c): ~4 chars per token for English-like
   prose, blended with a word count so very short/long tokens don't skew it.
   Deterministic; a real tokenizer can replace this later behind the same API.
   Returns 0 when len == 0. O(len). */
size_t receipt_estimate_tokens(const char *text, size_t len);

/* A before/after token accounting: `used_tokens` actually sent to the model
   vs `raw_tokens` a naive whole-file read would have cost. */
typedef struct { size_t used_tokens; size_t raw_tokens; } Receipt;

/* Percentage saved = (raw - used) / raw * 100, clamped to [0,100];
   returns 0 when raw == 0 or used >= raw. */
int receipt_saved_pct(const Receipt *r);

/* Format a human one-liner into `out` (NUL-terminated, never overflowing cap),
   e.g. "used 4231 tok · raw ≈ 128400 tok · saved 97%". */
void receipt_format(char *out, size_t cap, const Receipt *r);

/* Emit the receipt as a JSON object into `out` (NUL-terminated, bounded by cap),
   e.g. {"used_tokens":4231,"raw_tokens":128400,"saved_pct":97} */
void receipt_to_json(char *out, size_t cap, const Receipt *r);

#endif
