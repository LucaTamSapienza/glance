/* agent.c — JSON exports of Markdown structure for tools and agents. */
#include "agent.h"
#include "render.h"
#include "toc.h"
#include "vault.h"
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

/* Index of the vault file whose stem matches `target`, or -1. */
static int node_for(const char *target, VFiles *f) {
    char want[256]; vault_stem(target, want, sizeof want);
    for (int i = 0; i < f->n; i++) {
        char have[256]; vault_stem(f->v[i], have, sizeof have);
        if (strcasecmp(have, want) == 0) return i;
    }
    return -1;
}

int agent_graph(const char *dir) {
    DIR *probe = opendir(dir);
    if (!probe) return 1;                          /* unreadable root */
    closedir(probe);

    VFiles f = {0};
    vault_scan(dir, &f);                           /* recursive: all *.md under dir */

    printf("{\"nodes\":[");
    for (int i = 0; i < f.n; i++) { printf("%s", i ? "," : ""); json_str(f.v[i]); }
    printf("],\"edges\":[");

    int edges = 0;
    for (int i = 0; i < f.n; i++) {
        char path[8192];
        snprintf(path, sizeof path, "%s/%s", dir, f.v[i]);
        FILE *fp = fopen(path, "rb");
        if (!fp) continue;
        size_t len; char *src = read_file(fp, &len);
        fclose(fp);
        if (!src) continue;

        VLinks l = {0};
        vault_links(src, len, &l);
        for (int k = 0; k < l.n; k++) {
            int j = node_for(l.v[k].target, &f);
            if (j < 0) continue;                       /* external / unresolved */
            printf("%s{\"from\":", edges++ ? "," : "");
            json_str(f.v[i]); printf(",\"to\":");
            json_str(f.v[j]); printf(",\"wiki\":%s}", l.v[k].wiki ? "true" : "false");
        }
        vlinks_free(&l);
        free(src);
    }
    puts("]}");
    vfiles_free(&f);
    return 0;
}
