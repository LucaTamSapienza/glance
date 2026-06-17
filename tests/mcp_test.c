/* mcp_test.c — drives the MCP server one JSON-RPC line at a time, capturing the
 * responses on stdout (status is reported on stderr). Runs against testdata/vault
 * (cwd is the repo root under `make test`). */
#include "../src/mcp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    char tmp[] = "/tmp/glance_mcp_XXXXXX";
    int fd = mkstemp(tmp);
    if (fd < 0) { fprintf(stderr, "mkstemp failed\n"); return 1; }
    close(fd);
    if (!freopen(tmp, "w", stdout)) { fprintf(stderr, "freopen failed\n"); return 1; }

    mcp_handle_line("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2024-11-05\"}}");
    mcp_handle_line("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}");   /* no response */
    mcp_handle_line("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}");
    mcp_handle_line("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{\"name\":\"vault_context\",\"arguments\":{\"dir\":\"testdata/vault\",\"query\":\"rendering wikilinks\",\"budget\":120}}}");
    mcp_handle_line("{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/call\",\"params\":{\"name\":\"does_not_exist\",\"arguments\":{}}}");
    mcp_handle_line("{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"no/such/method\"}");
    mcp_handle_line("not json at all");
    fflush(stdout);

    FILE *r = fopen(tmp, "r");
    char *buf = malloc(1 << 18);
    size_t n = fread(buf, 1, (1 << 18) - 1, r); buf[n] = '\0';
    fclose(r);
    unlink(tmp);

    /* The notification produced no line, so there are exactly 6 responses. */
    int lines = 0;
    for (size_t i = 0; i < n; i++) if (buf[i] == '\n') lines++;

    int fails = 0;
    struct { const char *needle, *what; } checks[] = {
        {"\"serverInfo\":{\"name\":\"glance\"",     "initialize serverInfo"},
        {"\"protocolVersion\":\"2024-11-05\"",       "initialize protocol echo"},
        {"\"name\":\"vault_context\"",                "tools/list lists vault_context"},
        {"\"inputSchema\":",                          "tools/list has schemas"},
        {"\"content\":[{\"type\":\"text\"",          "tools/call content block"},
        {"\\\"saved_pct\\\":",                        "tools/call embeds the receipt"},
        {"\"code\":-32602",                           "unknown-tool error"},
        {"\"code\":-32601",                           "method-not-found error"},
        {"\"code\":-32700",                           "parse error"},
    };
    for (size_t i = 0; i < sizeof checks / sizeof checks[0]; i++)
        if (!strstr(buf, checks[i].needle)) { fprintf(stderr, "FAIL: %s\n", checks[i].what); fails++; }
    if (lines != 6) { fprintf(stderr, "FAIL: expected 6 response lines, got %d\n", lines); fails++; }

    free(buf);
    fprintf(stderr, fails ? "%d mcp test(s) FAILED\n" : "all mcp tests passed\n", fails);
    return fails ? 1 : 0;
}
