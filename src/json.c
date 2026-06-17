/* json.c — recursive-descent JSON parser (see json.h). */
#include "json.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

/* Parser cursor over a NUL-terminated buffer. */
typedef struct { const char *p; } P;

static Json *parse_value(P *s);

/* Advance past JSON whitespace. */
static void skip_ws(P *s) {
    while (*s->p == ' ' || *s->p == '\t' || *s->p == '\n' || *s->p == '\r') s->p++;
}

static Json *node(JsonType t) {
    Json *j = calloc(1, sizeof *j);
    if (j) j->type = t;
    return j;
}

/* Parse a JSON string body (the opening quote is already consumed) into an owned
 * decoded buffer, or NULL on error. On success *s points past the closing quote. */
static char *parse_string_raw(P *s) {
    size_t cap = 16, len = 0;
    char *out = malloc(cap);
    if (!out) return NULL;
    while (*s->p && *s->p != '"') {
        char c = *s->p++;
        if (len + 4 >= cap) { cap *= 2; char *t = realloc(out, cap); if (!t) { free(out); return NULL; } out = t; }
        if (c != '\\') { out[len++] = c; continue; }
        char e = *s->p++;
        switch (e) {
            case '"': out[len++] = '"'; break;
            case '\\': out[len++] = '\\'; break;
            case '/': out[len++] = '/'; break;
            case 'b': out[len++] = '\b'; break;
            case 'f': out[len++] = '\f'; break;
            case 'n': out[len++] = '\n'; break;
            case 'r': out[len++] = '\r'; break;
            case 't': out[len++] = '\t'; break;
            case 'u': {
                unsigned cp = 0;
                for (int i = 0; i < 4; i++) {
                    char h = *s->p++;
                    cp <<= 4;
                    if (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
                    else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
                    else { free(out); return NULL; }
                }
                len += (size_t)u8_encode(cp, out + len);   /* up to 4 bytes; cap headroom kept above */
                break;
            }
            default: free(out); return NULL;
        }
    }
    if (*s->p != '"') { free(out); return NULL; }
    s->p++;                       /* consume closing quote */
    out[len] = '\0';
    return out;
}

static Json *parse_string(P *s) {
    s->p++;                       /* opening quote */
    char *str = parse_string_raw(s);
    if (!str) return NULL;
    Json *j = node(JSON_STR);
    if (!j) { free(str); return NULL; }
    j->str = str;
    return j;
}

static Json *parse_number(P *s) {
    char *end;
    double v = strtod(s->p, &end);
    if (end == s->p) return NULL;
    s->p = end;
    Json *j = node(JSON_NUM);
    if (j) j->num = v;
    return j;
}

/* Append child (and optional key) to an array/object node; takes ownership. */
static int push_child(Json *j, char *key, Json *child) {
    Json **ni = realloc(j->items, (size_t)(j->n + 1) * sizeof *ni);
    if (!ni) return -1;
    j->items = ni;
    if (key) {
        char **nk = realloc(j->keys, (size_t)(j->n + 1) * sizeof *nk);
        if (!nk) return -1;
        j->keys = nk;
        j->keys[j->n] = key;
    }
    j->items[j->n] = child;
    j->n++;
    return 0;
}

static Json *parse_array(P *s) {
    s->p++;                       /* '[' */
    Json *j = node(JSON_ARR);
    if (!j) return NULL;
    skip_ws(s);
    if (*s->p == ']') { s->p++; return j; }
    for (;;) {
        Json *v = parse_value(s);
        if (!v || push_child(j, NULL, v) != 0) { json_free(v); json_free(j); return NULL; }
        skip_ws(s);
        if (*s->p == ',') { s->p++; skip_ws(s); continue; }
        if (*s->p == ']') { s->p++; return j; }
        json_free(j); return NULL;
    }
}

static Json *parse_object(P *s) {
    s->p++;                       /* '{' */
    Json *j = node(JSON_OBJ);
    if (!j) return NULL;
    skip_ws(s);
    if (*s->p == '}') { s->p++; return j; }
    for (;;) {
        skip_ws(s);
        if (*s->p != '"') { json_free(j); return NULL; }
        s->p++;
        char *key = parse_string_raw(s);
        if (!key) { json_free(j); return NULL; }
        skip_ws(s);
        if (*s->p != ':') { free(key); json_free(j); return NULL; }
        s->p++;
        Json *v = parse_value(s);
        if (!v || push_child(j, key, v) != 0) { free(key); json_free(v); json_free(j); return NULL; }
        skip_ws(s);
        if (*s->p == ',') { s->p++; continue; }
        if (*s->p == '}') { s->p++; return j; }
        json_free(j); return NULL;
    }
}

/* Parse a literal (true/false/null) into the given node type, or NULL. */
static Json *parse_literal(P *s, const char *word, JsonType t, int boolean) {
    size_t n = strlen(word);
    if (strncmp(s->p, word, n) != 0) return NULL;
    s->p += n;
    Json *j = node(t);
    if (j) j->boolean = boolean;
    return j;
}

static Json *parse_value(P *s) {
    skip_ws(s);
    switch (*s->p) {
        case '{': return parse_object(s);
        case '[': return parse_array(s);
        case '"': return parse_string(s);
        case 't': return parse_literal(s, "true", JSON_BOOL, 1);
        case 'f': return parse_literal(s, "false", JSON_BOOL, 0);
        case 'n': return parse_literal(s, "null", JSON_NULL, 0);
        default:  return parse_number(s);
    }
}

Json *json_parse(const char *text) {
    if (!text) return NULL;
    P s = { text };
    Json *j = parse_value(&s);
    if (!j) return NULL;
    skip_ws(&s);
    if (*s.p != '\0') { json_free(j); return NULL; }   /* trailing garbage */
    return j;
}

void json_free(Json *j) {
    if (!j) return;
    free(j->str);
    for (int i = 0; i < j->n; i++) {
        json_free(j->items[i]);
        if (j->keys) free(j->keys[i]);
    }
    free(j->items);
    free(j->keys);
    free(j);
}

const Json *json_get(const Json *obj, const char *key) {
    if (!obj || obj->type != JSON_OBJ || !obj->keys) return NULL;
    for (int i = 0; i < obj->n; i++)
        if (strcmp(obj->keys[i], key) == 0) return obj->items[i];
    return NULL;
}

const char *json_str_or(const Json *j, const char *def) {
    return (j && j->type == JSON_STR) ? j->str : def;
}

double json_num_or(const Json *j, double def) {
    return (j && j->type == JSON_NUM) ? j->num : def;
}

int json_bool_or(const Json *j, int def) {
    if (!j) return def;
    if (j->type == JSON_BOOL) return j->boolean;
    return def;
}
