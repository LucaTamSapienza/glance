/* agent_test.c — checks the JSON exports by capturing stdout to a temp file.
 * Test status is reported on stderr, since stdout is the captured stream. The
 * vault-scanning exports run against testdata/vault (cwd is the repo root under
 * `make test`). */
#include "../src/agent.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    char tmp[] = "/tmp/glance_agent_XXXXXX";
    int fd = mkstemp(tmp);
    if (fd < 0) { fprintf(stderr, "mkstemp failed\n"); return 1; }
    close(fd);

    if (!freopen(tmp, "w", stdout)) { fprintf(stderr, "freopen failed\n"); return 1; }

    const char *links = "see [a](a.md) and [[B]] and [x](https://e.io)";
    const char *doc   = "# H1\n\nbody\n\n## H2\n\nsecond section text\n";
    const char *vault = "testdata/vault";

    agent_links(links, strlen(links));
    agent_outline(doc, strlen(doc));
    agent_outline_ex(doc, strlen(doc), 1, 1);              /* depth 1 + abstract */
    agent_section(doc, strlen(doc), "H2");                 /* bounded read + receipt */
    agent_neighbors(vault, "Rendering", 1);
    agent_backlinks(vault, "Rendering", 1);                /* with context */
    agent_since(vault, 0);                                 /* everything is newer than epoch */
    agent_context(vault, "rendering markdown", 200);       /* budgeted bundle */
    fflush(stdout);

    FILE *r = fopen(tmp, "r");
    char *buf = malloc(1 << 18);
    size_t n = fread(buf, 1, (1 << 18) - 1, r); buf[n] = '\0';
    fclose(r);
    unlink(tmp);

    int fails = 0;
    struct { const char *needle, *what; } checks[] = {
        {"\"target\":\"a.md\",\"wiki\":false",  "markdown link"},
        {"\"target\":\"B\",\"wiki\":true",       "wikilink"},
        {"\"target\":\"https://e.io\"",           "external link"},
        {"\"level\":1,\"title\":\"H1\"",          "outline H1"},
        {"\"level\":2,\"title\":\"H2\"",          "outline H2"},
        {"\"abstract\":\"body\"",                 "outline abstract"},
        {"\"anchor\":\"H2\",\"found\":true",      "section found"},
        {"second section text",                   "section body text"},
        {"\"saved_pct\":",                        "section receipt"},
        {"\"found\":true,\"depth\":1",            "neighbors found"},
        {"\"direction\":",                        "neighbors direction"},
        {"\"backlinks\":[",                       "backlinks array"},
        {"\"context\":",                          "backlink context"},
        {"\"changed\":[",                         "since changed array"},
        {"\"mtime\":",                            "since mtime"},
        {"\"chunks\":[",                          "context chunks"},
        {"\"truncated\":[",                       "context truncation manifest"},
        {"\"receipt\":{\"used_tokens\":",         "context receipt"},
    };
    for (size_t i = 0; i < sizeof checks / sizeof checks[0]; i++)
        if (!strstr(buf, checks[i].needle)) { fprintf(stderr, "FAIL: %s\n", checks[i].what); fails++; }

    free(buf);
    fprintf(stderr, fails ? "%d agent test(s) FAILED\n" : "all agent tests passed\n", fails);
    return fails ? 1 : 0;
}
