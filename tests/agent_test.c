/* agent_test.c — checks the JSON exports by capturing stdout to a temp file.
 * Test status is reported on stderr, since stdout is the captured stream. */
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
    const char *out   = "# H1\n\nbody\n\n## H2\n";
    agent_links(links, strlen(links));
    agent_outline(out, strlen(out));
    fflush(stdout);

    FILE *r = fopen(tmp, "r");
    char buf[8192]; size_t n = fread(buf, 1, sizeof buf - 1, r); buf[n] = '\0';
    fclose(r);
    unlink(tmp);

    int fails = 0;
    struct { const char *needle, *what; } checks[] = {
        {"\"target\":\"a.md\",\"wiki\":false", "markdown link"},
        {"\"target\":\"B\",\"wiki\":true",      "wikilink"},
        {"\"target\":\"https://e.io\"",          "external link"},
        {"\"level\":1,\"title\":\"H1\"",         "outline H1"},
        {"\"level\":2,\"title\":\"H2\"",         "outline H2"},
    };
    for (size_t i = 0; i < sizeof checks / sizeof checks[0]; i++)
        if (!strstr(buf, checks[i].needle)) { fprintf(stderr, "FAIL: %s\n", checks[i].what); fails++; }

    fprintf(stderr, fails ? "%d agent test(s) FAILED\n" : "all agent tests passed\n", fails);
    return fails ? 1 : 0;
}
