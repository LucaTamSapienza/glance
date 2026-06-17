/* mcp.c — MCP server over stdio (see mcp.h). */
#include "mcp.h"
#include "json.h"
#include "agent.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MCP_NAME            "glance"
#define MCP_VERSION         "0.1.0"
#define MCP_PROTOCOL_FALLBACK "2024-11-05"

/* ---- small JSON output helpers (writing is done by hand) ------------------ */

/* Emit `s` as a JSON string literal (with surrounding quotes) to stdout. */
static void emit_jstr(const char *s) {
    putchar('"');
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { putchar('\\'); putchar((char)c); }
        else if (c == '\n') fputs("\\n", stdout);
        else if (c == '\r') fputs("\\r", stdout);
        else if (c == '\t') fputs("\\t", stdout);
        else if (c < 0x20) printf("\\u%04x", c);
        else putchar((char)c);
    }
    putchar('"');
}

/* Emit a request id verbatim: a JSON-RPC id is a number, a string, or null. */
static void emit_id(const Json *id) {
    if (!id) { fputs("null", stdout); return; }
    if (id->type == JSON_NUM) {
        double d = id->num;
        if (d == (double)(long long)d) printf("%lld", (long long)d);
        else printf("%g", d);
    } else if (id->type == JSON_STR) {
        emit_jstr(id->str);
    } else {
        fputs("null", stdout);
    }
}

/* ---- capturing an export's stdout ----------------------------------------- */

/* Redirect stdout to a fresh temp file; *saved keeps the real stdout fd. */
static FILE *cap_begin(int *saved) {
    fflush(stdout);
    *saved = dup(fileno(stdout));
    FILE *tmp = tmpfile();
    if (tmp) dup2(fileno(tmp), fileno(stdout));
    return tmp;
}

/* Restore stdout and return the captured text (owned), or "" on failure. */
static char *cap_end(int saved, FILE *tmp) {
    fflush(stdout);
    dup2(saved, fileno(stdout));
    close(saved);
    if (!tmp) return strdup("");
    rewind(tmp);
    size_t len;
    char *out = read_file(tmp, &len);
    fclose(tmp);
    return out ? out : strdup("");
}

/* Read a whole file into a NUL-terminated buffer; *len gets the length. */
static char *load_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *s = read_file(f, len);
    fclose(f);
    return s;
}

/* ---- tools ---------------------------------------------------------------- */

/* Run the named tool with `args`, returning its captured JSON output (owned),
 * or NULL if the tool name is unknown. */
static char *run_tool(const char *name, const Json *args) {
    int known = 1, saved;
    FILE *tmp = cap_begin(&saved);

    if (!strcmp(name, "vault_context")) {
        agent_context(json_str_or(json_get(args, "dir"), "."),
                      json_str_or(json_get(args, "query"), ""),
                      (size_t)json_num_or(json_get(args, "budget"), 0),
                      json_bool_or(json_get(args, "semantic"), 0));
    } else if (!strcmp(name, "vault_neighbors")) {
        agent_neighbors(json_str_or(json_get(args, "dir"), "."),
                        json_str_or(json_get(args, "note"), ""),
                        (int)json_num_or(json_get(args, "depth"), 1));
    } else if (!strcmp(name, "vault_backlinks")) {
        agent_backlinks(json_str_or(json_get(args, "dir"), "."),
                        json_str_or(json_get(args, "note"), ""),
                        json_bool_or(json_get(args, "context"), 0));
    } else if (!strcmp(name, "vault_since")) {
        agent_since(json_str_or(json_get(args, "dir"), "."),
                    (long)json_num_or(json_get(args, "since"), 0));
    } else if (!strcmp(name, "vault_graph")) {
        agent_graph(json_str_or(json_get(args, "dir"), "."));
    } else if (!strcmp(name, "vault_section")) {
        const char *file = json_str_or(json_get(args, "file"), "");
        const char *heading = json_str_or(json_get(args, "heading"), NULL);
        size_t len; char *src = load_file(file, &len);
        if (src) { agent_section(src, len, heading); free(src); }
        else printf("{\"error\":\"cannot read file\"}");
    } else if (!strcmp(name, "vault_outline")) {
        const char *file = json_str_or(json_get(args, "file"), "");
        size_t len; char *src = load_file(file, &len);
        if (src) { agent_outline_ex(src, len, (int)json_num_or(json_get(args, "depth"), 0),
                                     json_bool_or(json_get(args, "abstract"), 0)); free(src); }
        else printf("{\"error\":\"cannot read file\"}");
    } else if (!strcmp(name, "vault_links")) {
        const char *file = json_str_or(json_get(args, "file"), "");
        size_t len; char *src = load_file(file, &len);
        if (src) { agent_links(src, len); free(src); }
        else printf("{\"error\":\"cannot read file\"}");
    } else if (!strcmp(name, "vault_edit")) {
        const char *opname = json_str_or(json_get(args, "op"), "append");
        int op = !strcmp(opname, "insert") ? 1 : !strcmp(opname, "replace") ? 2 : 0;
        agent_edit(json_str_or(json_get(args, "file"), ""),
                   json_str_or(json_get(args, "heading"), NULL),
                   op, json_str_or(json_get(args, "text"), ""));
    } else if (!strcmp(name, "vault_set_frontmatter")) {
        agent_frontmatter(json_str_or(json_get(args, "file"), ""),
                          json_str_or(json_get(args, "key"), ""),
                          json_str_or(json_get(args, "value"), ""));
    } else {
        known = 0;
    }

    char *out = cap_end(saved, tmp);
    if (!known) { free(out); return NULL; }
    return out;
}

/* One tool's schema line: name, description, and a JSON Schema for its args. */
typedef struct { const char *name, *desc, *schema; } ToolDef;

static const ToolDef TOOLS[] = {
    { "vault_context",
      "Retrieve a token-cheap, budget-bounded context bundle for a query over a Markdown vault: ranked note sections (BM25 + a link-graph prior), with diversity, coarse-to-fine projection, a truncation manifest, and a token receipt.",
      "{\"type\":\"object\",\"properties\":{\"dir\":{\"type\":\"string\",\"description\":\"vault directory\"},\"query\":{\"type\":\"string\"},\"budget\":{\"type\":\"integer\",\"description\":\"max tokens (0 = no cap)\"},\"semantic\":{\"type\":\"boolean\",\"description\":\"fuse embedding similarity with the lexical score\"}},\"required\":[\"dir\",\"query\"]}" },
    { "vault_section",
      "Return one heading's subtree from a Markdown file (matched by heading text or slug), plus a token receipt versus reading the whole file.",
      "{\"type\":\"object\",\"properties\":{\"file\":{\"type\":\"string\"},\"heading\":{\"type\":\"string\"}},\"required\":[\"file\"]}" },
    { "vault_outline",
      "Print a file's heading tree, depth-bounded, optionally with each heading's first paragraph as an abstract.",
      "{\"type\":\"object\",\"properties\":{\"file\":{\"type\":\"string\"},\"depth\":{\"type\":\"integer\"},\"abstract\":{\"type\":\"boolean\"}},\"required\":[\"file\"]}" },
    { "vault_neighbors",
      "The link-graph neighbourhood of a note out to N hops, with direction (outbound/backlink/both) for direct neighbours.",
      "{\"type\":\"object\",\"properties\":{\"dir\":{\"type\":\"string\"},\"note\":{\"type\":\"string\"},\"depth\":{\"type\":\"integer\"}},\"required\":[\"dir\",\"note\"]}" },
    { "vault_backlinks",
      "The notes that link to a given note; with context=true, each backlink carries the source line that cites it.",
      "{\"type\":\"object\",\"properties\":{\"dir\":{\"type\":\"string\"},\"note\":{\"type\":\"string\"},\"context\":{\"type\":\"boolean\"}},\"required\":[\"dir\",\"note\"]}" },
    { "vault_since",
      "The notes in a vault modified after a Unix timestamp — the cheap 'what changed?' delta.",
      "{\"type\":\"object\",\"properties\":{\"dir\":{\"type\":\"string\"},\"since\":{\"type\":\"integer\"}},\"required\":[\"dir\",\"since\"]}" },
    { "vault_links",
      "The outbound links of a Markdown file (Markdown links and [[wikilinks]]).",
      "{\"type\":\"object\",\"properties\":{\"file\":{\"type\":\"string\"}},\"required\":[\"file\"]}" },
    { "vault_graph",
      "The whole vault's link graph as {nodes,edges}.",
      "{\"type\":\"object\",\"properties\":{\"dir\":{\"type\":\"string\"}},\"required\":[\"dir\"]}" },
    { "vault_edit",
      "Surgically edit a section of a Markdown file and save it atomically: op=append adds text at the end of the section under the given heading, insert adds it right after the heading, replace swaps the section body. All other formatting is preserved.",
      "{\"type\":\"object\",\"properties\":{\"file\":{\"type\":\"string\"},\"heading\":{\"type\":\"string\"},\"op\":{\"type\":\"string\",\"enum\":[\"append\",\"insert\",\"replace\"]},\"text\":{\"type\":\"string\"}},\"required\":[\"file\",\"heading\",\"text\"]}" },
    { "vault_set_frontmatter",
      "Set a YAML frontmatter key to a value in a Markdown file (updating the key, adding it, or creating the frontmatter block), saved atomically.",
      "{\"type\":\"object\",\"properties\":{\"file\":{\"type\":\"string\"},\"key\":{\"type\":\"string\"},\"value\":{\"type\":\"string\"}},\"required\":[\"file\",\"key\",\"value\"]}" },
};
static const int NTOOLS = (int)(sizeof TOOLS / sizeof TOOLS[0]);

/* ---- request handlers ----------------------------------------------------- */

/* Emit the standard response envelope head: {"jsonrpc":"2.0","id":<id>, */
static void emit_head(const Json *id) {
    fputs("{\"jsonrpc\":\"2.0\",\"id\":", stdout);
    emit_id(id);
    putchar(',');
}

static void respond_error(const Json *id, int code, const char *msg) {
    emit_head(id);
    printf("\"error\":{\"code\":%d,\"message\":", code);
    emit_jstr(msg);
    fputs("}}\n", stdout);
    fflush(stdout);
}

static void handle_initialize(const Json *id, const Json *params) {
    const char *proto = json_str_or(json_get(params, "protocolVersion"), MCP_PROTOCOL_FALLBACK);
    emit_head(id);
    fputs("\"result\":{\"protocolVersion\":", stdout);
    emit_jstr(proto);
    fputs(",\"capabilities\":{\"tools\":{}},\"serverInfo\":{\"name\":\"" MCP_NAME
          "\",\"version\":\"" MCP_VERSION "\"}}}\n", stdout);
    fflush(stdout);
}

static void handle_tools_list(const Json *id) {
    emit_head(id);
    fputs("\"result\":{\"tools\":[", stdout);
    for (int i = 0; i < NTOOLS; i++) {
        if (i) putchar(',');
        fputs("{\"name\":", stdout);
        emit_jstr(TOOLS[i].name);
        fputs(",\"description\":", stdout);
        emit_jstr(TOOLS[i].desc);
        fputs(",\"inputSchema\":", stdout);
        fputs(TOOLS[i].schema, stdout);   /* already valid JSON */
        putchar('}');
    }
    fputs("]}}\n", stdout);
    fflush(stdout);
}

static void handle_tools_call(const Json *id, const Json *params) {
    const char *name = json_str_or(json_get(params, "name"), "");
    const Json *args = json_get(params, "arguments");
    char *out = run_tool(name, args);
    if (!out) { respond_error(id, -32602, "unknown tool"); return; }

    emit_head(id);
    fputs("\"result\":{\"content\":[{\"type\":\"text\",\"text\":", stdout);
    emit_jstr(out);
    fputs("}],\"isError\":false}}\n", stdout);
    fflush(stdout);
    free(out);
}

/* Dispatch one parsed message. Notifications (no id) get no response. */
static void handle(const Json *msg) {
    const Json *id = json_get(msg, "id");
    const char *method = json_str_or(json_get(msg, "method"), "");
    const Json *params = json_get(msg, "params");

    if (!strcmp(method, "initialize"))            handle_initialize(id, params);
    else if (!strcmp(method, "tools/list"))       handle_tools_list(id);
    else if (!strcmp(method, "tools/call"))       handle_tools_call(id, params);
    else if (!strcmp(method, "ping"))             { emit_head(id); fputs("\"result\":{}}\n", stdout); fflush(stdout); }
    else if (!strncmp(method, "notifications/", 14)) { /* no response */ }
    else if (id)                                  respond_error(id, -32601, "method not found");
}

int mcp_handle_line(const char *line) {
    Json *msg = json_parse(line);
    if (!msg) { respond_error(NULL, -32700, "parse error"); return -1; }
    handle(msg);
    json_free(msg);
    return 0;
}

int mcp_serve(void) {
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    while ((n = getline(&line, &cap, stdin)) != -1) {
        if (n == 0 || line[0] == '\n' || line[0] == '\r') continue;   /* skip blanks */
        mcp_handle_line(line);
    }
    free(line);
    return 0;
}
