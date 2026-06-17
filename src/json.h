#ifndef GLANCE_JSON_H
#define GLANCE_JSON_H

/* json.c — a small, dependency-free JSON parser.
 *
 * glance owns its renderer; for the MCP server (mcp.c) it also owns a minimal
 * JSON reader rather than pulling in a library. It parses a NUL-terminated text
 * into a tagged value tree and offers a few typed accessors. Writing JSON is
 * done directly with printf elsewhere (the agent exports), so this module only
 * reads. */

typedef enum { JSON_NULL, JSON_BOOL, JSON_NUM, JSON_STR, JSON_ARR, JSON_OBJ } JsonType;

typedef struct Json Json;
struct Json {
    JsonType type;
    int      boolean;   /* JSON_BOOL */
    double   num;       /* JSON_NUM  */
    char    *str;       /* JSON_STR: decoded UTF-8, owned */
    Json   **items;     /* JSON_ARR/OBJ: child values, owned */
    char   **keys;      /* JSON_OBJ: member keys (parallel to items), owned */
    int      n;         /* JSON_ARR/OBJ: child count */
};

/* Parse a NUL-terminated JSON text into an owned tree, or NULL on malformed
 * input or OOM. Free with json_free. */
Json *json_parse(const char *text);

void json_free(Json *j);

/* The value of object member `key`, or NULL if `obj` is not an object or has no
 * such member. */
const Json *json_get(const Json *obj, const char *key);

/* Typed accessors that tolerate a NULL or wrong-typed node by returning `def`. */
const char *json_str_or(const Json *j, const char *def);
double      json_num_or(const Json *j, double def);
int         json_bool_or(const Json *j, int def);

#endif /* GLANCE_JSON_H */
