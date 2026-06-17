/* json.c — recursive-descent JSON parser (see json.h). */
#include "json.h"
#include "util.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Cap on nesting so a crafted deeply-nested document (e.g. thousands of '[')
 * cannot overflow the C stack — the MCP server parses untrusted input. */
#define JSON_MAX_DEPTH 200

/* Parser cursor over a NUL-terminated buffer; `depth` tracks container nesting. */
typedef struct { const char *p; int depth; } P;

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

/* Read exactly four hex digits at s->p, advancing past them; -1 if not all hex. */
static long read_hex4(P *s) {
    long v = 0;
    for (int i = 0; i < 4; i++) {
        char h = *s->p++;
        v <<= 4;
        if (h >= '0' && h <= '9') v |= h - '0';
        else if (h >= 'a' && h <= 'f') v |= h - 'a' + 10;
        else if (h >= 'A' && h <= 'F') v |= h - 'A' + 10;
        else return -1;
    }
    return v;
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
                long cp = read_hex4(s);
                if (cp < 0) { free(out); return NULL; }
                if (cp >= 0xD800 && cp <= 0xDBFF) {            /* high surrogate */
                    if (s->p[0] != '\\' || s->p[1] != 'u') { free(out); return NULL; }
                    s->p += 2;
                    long lo = read_hex4(s);
                    if (lo < 0xDC00 || lo > 0xDFFF) { free(out); return NULL; }
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {     /* lone low surrogate */
                    free(out); return NULL;
                } else if (cp == 0) {                          /* interior NUL truncates */
                    free(out); return NULL;
                }
                len += (size_t)u8_encode((uint32_t)cp, out + len);   /* ≤4 bytes; headroom kept above */
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

/* Length of a valid JSON number at `p` (RFC 8259 grammar), or 0 if none. This
 * rejects what strtod would otherwise accept — inf/nan, a leading '+', leading
 * zeros, a bare '.5' or '1.', hex floats. */
static int json_number_len(const char *p) {
    const char *s = p;
    if (*p == '-') p++;
    if (*p == '0') p++;
    else if (*p >= '1' && *p <= '9') { while (*p >= '0' && *p <= '9') p++; }
    else return 0;
    if (*p == '.') { p++; if (!(*p >= '0' && *p <= '9')) return 0; while (*p >= '0' && *p <= '9') p++; }
    if (*p == 'e' || *p == 'E') {
        p++;
        if (*p == '+' || *p == '-') p++;
        if (!(*p >= '0' && *p <= '9')) return 0;
        while (*p >= '0' && *p <= '9') p++;
    }
    return (int)(p - s);
}

static Json *parse_number(P *s) {
    int n = json_number_len(s->p);
    if (n == 0) return NULL;
    double v = strtod(s->p, NULL);
    if (!isfinite(v)) return NULL;        /* e.g. 1e9999 overflows to inf */
    s->p += n;
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
    if (*s->p == '{' || *s->p == '[') {
        if (s->depth >= JSON_MAX_DEPTH) return NULL;   /* too deeply nested */
        s->depth++;
        Json *j = (*s->p == '{') ? parse_object(s) : parse_array(s);
        s->depth--;
        return j;
    }
    switch (*s->p) {
        case '"': return parse_string(s);
        case 't': return parse_literal(s, "true", JSON_BOOL, 1);
        case 'f': return parse_literal(s, "false", JSON_BOOL, 0);
        case 'n': return parse_literal(s, "null", JSON_NULL, 0);
        default:  return parse_number(s);
    }
}

Json *json_parse(const char *text) {
    if (!text) return NULL;
    P s = { text, 0 };
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
