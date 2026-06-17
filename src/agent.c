/* agent.c — JSON exports of Markdown structure for tools and agents. */
#include "agent.h"
#include "render.h"
#include "toc.h"
#include "section.h"
#include "receipt.h"
#include "vault.h"
#include "graph.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>

/* Print s as a JSON string literal, escaping as needed. */
static void json_str(const char *s) {
    putchar('"');
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { putchar('\\'); putchar((char)c); }
        else if (c == '\n') fputs("\\n", stdout);
        else if (c == '\t') fputs("\\t", stdout);
        else if (c < 0x20) printf("\\u%04x", c);
        else putchar((char)c);
    }
    putchar('"');
}

void agent_outline(const char *src, size_t len) {
    Doc *d = render_doc(src, len, 100000, 1);   /* huge width: no wrapping */
    TOC t = {0};
    toc_build(d, &t);
    putchar('[');
    for (int i = 0; i < t.n; i++) {
        printf("%s{\"level\":%d,\"title\":", i ? "," : "", t.v[i].level);
        json_str(t.v[i].title);
        printf(",\"line\":%d}", t.v[i].line);
    }
    puts("]");
    toc_free(&t);
    doc_free(d);
}

void agent_section(const char *src, size_t len, const char *anchor) {
    Doc *d = render_doc(src, len, 100000, 1);   /* huge width: no wrapping */
    Section s = section_find(d, anchor);

    printf("{\"anchor\":");
    json_str(anchor ? anchor : "");
    printf(",\"found\":%s", s.found ? "true" : "false");

    if (s.found) {
        char *text = section_text(d, s.start, s.end);
        Receipt r = {
            text ? receipt_estimate_tokens(text, strlen(text)) : 0,
            receipt_estimate_tokens(src, len),
        };
        char rj[160];
        receipt_to_json(rj, sizeof rj, &r);
        printf(",\"level\":%d,\"start_line\":%d,\"end_line\":%d,\"text\":",
               s.level, s.start, s.end);
        json_str(text ? text : "");
        printf(",\"receipt\":%s", rj);
        free(text);
    }
    puts("}");
    doc_free(d);
}

void agent_links(const char *src, size_t len) {
    VLinks l = {0};
    vault_links(src, len, &l);
    putchar('[');
    for (int i = 0; i < l.n; i++) {
        printf("%s{\"target\":", i ? "," : "");
        json_str(l.v[i].target);
        printf(",\"wiki\":%s}", l.v[i].wiki ? "true" : "false");
    }
    puts("]");
    vlinks_free(&l);
}

/* ---- vault graph ---------------------------------------------------------- */

int agent_graph(const char *dir) {
    DIR *probe = opendir(dir);
    if (!probe) return 1;                          /* unreadable root */
    closedir(probe);

    Graph g;
    graph_build(dir, &g);                          /* shared with the TUI graph view */

    printf("{\"nodes\":[");
    for (int i = 0; i < g.nn; i++) { printf("%s", i ? "," : ""); json_str(g.node[i]); }
    printf("],\"edges\":[");
    for (int e = 0; e < g.ne; e++) {
        printf("%s{\"from\":", e ? "," : "");
        json_str(g.node[g.edge[e].from]); printf(",\"to\":");
        json_str(g.node[g.edge[e].to]); printf(",\"wiki\":%s}", g.edge[e].wiki ? "true" : "false");
    }
    puts("]}");
    graph_free(&g);
    return 0;
}
