/* Unit tests for json.c — the minimal JSON parser. */
#include "../src/json.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    /* A realistic MCP tools/call request. */
    const char *req =
        "{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"vault_context\",\"arguments\":"
        "{\"dir\":\"./vault\",\"query\":\"deploy\",\"budget\":4000,\"abstract\":true}}}";
    Json *j = json_parse(req);
    assert(j && j->type == JSON_OBJ);
    assert(strcmp(json_str_or(json_get(j, "method"), ""), "tools/call") == 0);
    assert(json_num_or(json_get(j, "id"), -1) == 7);

    const Json *params = json_get(j, "params");
    assert(strcmp(json_str_or(json_get(params, "name"), ""), "vault_context") == 0);
    const Json *args = json_get(params, "arguments");
    assert(strcmp(json_str_or(json_get(args, "dir"), ""), "./vault") == 0);
    assert(strcmp(json_str_or(json_get(args, "query"), ""), "deploy") == 0);
    assert(json_num_or(json_get(args, "budget"), 0) == 4000);
    assert(json_bool_or(json_get(args, "abstract"), 0) == 1);
    /* missing field → default */
    assert(json_num_or(json_get(args, "nope"), 42) == 42);
    json_free(j);

    /* Arrays and nested values. */
    Json *a = json_parse("[1, \"two\", true, null, [3]]");
    assert(a && a->type == JSON_ARR && a->n == 5);
    assert(a->items[0]->type == JSON_NUM && a->items[0]->num == 1);
    assert(strcmp(a->items[1]->str, "two") == 0);
    assert(a->items[2]->type == JSON_BOOL && a->items[2]->boolean == 1);
    assert(a->items[3]->type == JSON_NULL);
    assert(a->items[4]->type == JSON_ARR && a->items[4]->n == 1);
    json_free(a);

    /* String escapes, including \u (decoded to UTF-8). */
    Json *s = json_parse("\"a\\tb\\n\\\"q\\\" \\u00e9\"");
    assert(s && s->type == JSON_STR);
    assert(strcmp(s->str, "a\tb\n\"q\" \xc3\xa9") == 0);   /* é = U+00E9 */
    json_free(s);

    /* Negative / fractional numbers. */
    Json *n = json_parse("-12.5");
    assert(n && n->type == JSON_NUM && n->num == -12.5);
    json_free(n);

    /* Malformed inputs must return NULL, not crash. */
    assert(json_parse("{") == NULL);
    assert(json_parse("{\"a\":}") == NULL);
    assert(json_parse("[1,]") == NULL);
    assert(json_parse("nul") == NULL);
    assert(json_parse("\"unterminated") == NULL);
    assert(json_parse("{\"a\":1} trailing") == NULL);
    assert(json_parse("") == NULL);

    printf("all json tests passed\n");
    return 0;
}
