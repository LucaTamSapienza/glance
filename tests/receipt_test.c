/* receipt_test.c — unit tests for the token receipt logic. */
#include "../src/receipt.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    /* estimate: empty is 0 */
    assert(receipt_estimate_tokens(NULL, 0) == 0);
    assert(receipt_estimate_tokens("", 0) == 0);

    /* estimate: plain ascii prose lands near chars/4 */
    const char *prose = "the quick brown fox jumps over the lazy dog";
    size_t n = strlen(prose);
    size_t est = receipt_estimate_tokens(prose, n);
    assert(est >= n / 5 && est <= n / 2);   /* roughly chars/4, generously */

    /* estimate: word count is a floor — many one-char words beat bytes/4 */
    const char *tiny = "a b c d e f g h";
    assert(receipt_estimate_tokens(tiny, strlen(tiny)) >= 8);

    /* estimate: monotonic-ish — more text is never fewer tokens */
    size_t e1 = receipt_estimate_tokens(prose, n);
    char doubled[256];
    snprintf(doubled, sizeof doubled, "%s %s", prose, prose);
    size_t e2 = receipt_estimate_tokens(doubled, strlen(doubled));
    assert(e2 >= e1);

    /* saved_pct edge cases */
    Receipt zero = { 0, 0 };
    assert(receipt_saved_pct(&zero) == 0);                 /* raw == 0 */
    Receipt over = { 200, 100 };
    assert(receipt_saved_pct(&over) == 0);                 /* used >= raw */
    Receipt eq = { 100, 100 };
    assert(receipt_saved_pct(&eq) == 0);                   /* used == raw */
    Receipt all = { 0, 100 };
    assert(receipt_saved_pct(&all) == 100);                /* used == 0 */
    Receipt big = { 4231, 128400 };
    int pct = receipt_saved_pct(&big);
    assert(pct >= 96 && pct <= 97);

    /* receipt_format: expected substrings, never overflows a tiny buffer */
    char buf[128];
    receipt_format(buf, sizeof buf, &big);
    assert(strstr(buf, "used") != NULL);
    assert(strstr(buf, "saved") != NULL);
    assert(strstr(buf, "4231") != NULL);

    char tinybuf[8];
    receipt_format(tinybuf, sizeof tinybuf, &big);
    assert(strlen(tinybuf) < sizeof tinybuf);              /* bounded + NUL */

    /* receipt_to_json: valid-looking keys, never overflows a tiny buffer */
    char json[128];
    receipt_to_json(json, sizeof json, &big);
    assert(strstr(json, "\"used_tokens\":4231") != NULL);
    assert(strstr(json, "\"raw_tokens\":128400") != NULL);
    assert(strstr(json, "\"saved_pct\":") != NULL);
    assert(json[0] == '{' && json[strlen(json) - 1] == '}');

    char tinyjson[8];
    receipt_to_json(tinyjson, sizeof tinyjson, &big);
    assert(strlen(tinyjson) < sizeof tinyjson);            /* bounded + NUL */

    printf("all receipt tests passed\n");
    return 0;
}
