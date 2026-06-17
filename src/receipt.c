/* receipt.c — pure logic for the token receipt (see receipt.h).
 * No tokenizer, no clock, no globals: just arithmetic and two snprintf calls. */
#include "receipt.h"

#include <stdio.h>

/* Estimate tokens with a blended heuristic. Two cheap signals correlate with a
 * real BPE token count: the byte length (English-like prose averages ~4 bytes
 * per token) and the whitespace-delimited word count (a floor, since almost
 * every word is at least one token). Taking the max of bytes/4 and the word
 * count keeps both very dense text (long words → bytes/4 dominates) and very
 * sparse text (many tiny words → word count dominates) honest. It is only an
 * estimate; swap in a true tokenizer behind this signature when one is wired. */
size_t receipt_estimate_tokens(const char *text, size_t len) {
    if (!text || len == 0) return 0;

    size_t words = 0;
    int in_word = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)text[i];
        int space = (c == ' ' || c == '\t' || c == '\n' ||
                     c == '\r' || c == '\f' || c == '\v');
        if (!space && !in_word) { words++; in_word = 1; }
        else if (space) { in_word = 0; }
    }

    size_t by_bytes = (len + 3) / 4;   /* ceil(len / 4) */
    return by_bytes > words ? by_bytes : words;
}

int receipt_saved_pct(const Receipt *r) {
    if (!r || r->raw_tokens == 0 || r->used_tokens >= r->raw_tokens) return 0;
    size_t saved = r->raw_tokens - r->used_tokens;
    int pct = (int)((unsigned long long)saved * 100 / r->raw_tokens);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}

void receipt_format(char *out, size_t cap, const Receipt *r) {
    if (!out || cap == 0) return;
    if (!r) { out[0] = '\0'; return; }
    snprintf(out, cap,
             "used %zu tok \xc2\xb7 raw \xe2\x89\x88 %zu tok \xc2\xb7 saved %d%%",
             r->used_tokens, r->raw_tokens, receipt_saved_pct(r));
}

void receipt_to_json(char *out, size_t cap, const Receipt *r) {
    if (!out || cap == 0) return;
    if (!r) { out[0] = '\0'; return; }
    snprintf(out, cap,
             "{\"used_tokens\":%zu,\"raw_tokens\":%zu,\"saved_pct\":%d}",
             r->used_tokens, r->raw_tokens, receipt_saved_pct(r));
}
