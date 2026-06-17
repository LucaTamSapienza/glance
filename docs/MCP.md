# glance as an MCP server

`glance mcp` turns glance into an [MCP](https://modelcontextprotocol.io) server:
it speaks newline-delimited JSON-RPC 2.0 over stdio and exposes the agent-memory
reads as native tools, so any MCP client — Claude Desktop, Cursor, the Agent SDK
— can read a Markdown vault for a fraction of the tokens a raw file read costs.

This is the distribution wedge of the M2 milestone (see `DESIGN.md`). The tool
bodies are the exact same `glance --…` exports; the server just frames them as
JSON-RPC, so the CLI and the MCP surface never drift.

## Connect it

### Claude Desktop
Add glance to `claude_desktop_config.json` (macOS:
`~/Library/Application Support/Claude/claude_desktop_config.json`):

```json
{
  "mcpServers": {
    "glance": {
      "command": "glance",
      "args": ["mcp"]
    }
  }
}
```

Use the absolute path (`/usr/local/bin/glance` or `~/.local/bin/glance`) if
`glance` is not on the launcher's `PATH`. Restart Claude Desktop; the glance
tools then appear in any chat.

### Cursor / other MCP clients
Point the client at the same command — `glance mcp` over stdio. Cursor:
Settings → MCP → add a server with command `glance`, args `["mcp"]`.

### Try it by hand
The server is just stdio JSON-RPC, so you can drive it from a shell:

```sh
printf '%s\n' \
  '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05"}}' \
  '{"jsonrpc":"2.0","id":2,"method":"tools/list"}' \
  '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"vault_context","arguments":{"dir":"./my-vault","query":"how do we deploy?","budget":4000}}}' \
  | glance mcp
```

## Tools

Every read is **bounded** so it stays token-cheap; `vault_context` and
`vault_section` also return a token **receipt** (used vs whole-file-read tokens).

| Tool | Arguments | Returns |
|------|-----------|---------|
| `vault_context`   | `dir`, `query`, `budget?` | budgeted retrieval bundle: ranked sections (BM25 + a link-graph prior), with diversity, coarse-to-fine projection, a truncation manifest, and a receipt |
| `vault_section`   | `file`, `heading?`        | one heading's subtree + a token receipt |
| `vault_outline`   | `file`, `depth?`, `abstract?` | the heading tree, depth-bounded, optional per-heading abstract |
| `vault_neighbors` | `dir`, `note`, `depth?`   | link-graph neighbourhood with direction |
| `vault_backlinks` | `dir`, `note`, `context?` | who links here, optionally with the citing line |
| `vault_since`     | `dir`, `since`            | notes modified after a Unix timestamp |
| `vault_links`     | `file`                    | a file's outbound links |
| `vault_graph`     | `dir`                     | the whole vault's link graph |

Each tool's result is a text content block whose text is the same JSON the
corresponding `glance --…` command prints, so an agent can parse it directly.

## Protocol notes

- **Transport:** stdio, one JSON-RPC message per line (no embedded newlines).
- **Methods:** `initialize`, `tools/list`, `tools/call`, `ping`, and
  `notifications/*` (notifications get no response). `initialize` echoes the
  client's `protocolVersion`.
- **Errors:** standard JSON-RPC codes — `-32700` parse error, `-32601` method not
  found, `-32602` unknown tool.
- **No write tools yet:** the M2 surface is read-only. Surgical writes
  (`vault_edit`, frontmatter) arrive with M4.
