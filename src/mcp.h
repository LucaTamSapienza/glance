#ifndef GLANCE_MCP_H
#define GLANCE_MCP_H

/* mcp.c — an MCP (Model Context Protocol) server over stdio.
 *
 * Speaks newline-delimited JSON-RPC 2.0 on stdin/stdout and exposes glance's
 * agent-memory reads (outline/section/context/neighbors/backlinks/since/links/
 * graph) as MCP tools, so any MCP client — Claude Desktop, Cursor, the SDK —
 * can use a vault natively. The tool bodies reuse the exact agent.c exports;
 * the server just frames them as JSON-RPC. */

/* Serve until stdin reaches EOF. Returns 0. */
int mcp_serve(void);

/* Parse and dispatch a single JSON-RPC line, writing any response to stdout.
 * The testable seam behind mcp_serve's loop. Returns 0, or -1 on a parse error
 * (after emitting the JSON-RPC parse-error response). */
int mcp_handle_line(const char *line);

#endif /* GLANCE_MCP_H */
