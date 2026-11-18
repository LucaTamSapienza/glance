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

### Claude Code
```sh
claude mcp add -s user glance -- glance mcp   # every project; drop "-s user" for this project only
```

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

Reads are projections (a section, an outline, a neighbourhood) rather than
whole files; `vault_context` and `vault_section` also return a token **receipt**
(estimated tokens used vs a whole-file read). **Always pass `budget` to
`vault_context`:** it defaults to 0, which means no cap and returns every
matching section.

| Tool | Arguments | Returns |
|------|-----------|---------|
| `vault_context`   | `dir`, `query`, `budget?` (default 0 = no cap; set it), `semantic?` | budgeted retrieval bundle: ranked sections (BM25 + a link-graph prior), with diversity, coarse-to-fine projection, a truncation manifest, and a receipt |
| `vault_section`   | `file`, `heading?`        | one heading's subtree + a token receipt |
| `vault_outline`   | `file`, `depth?`, `abstract?` | the heading tree, depth-bounded, optional per-heading abstract |
| `vault_neighbors` | `dir`, `note`, `depth?`   | link-graph neighbourhood with direction |
| `vault_backlinks` | `dir`, `note`, `context?` | who links here, optionally with the citing line |
| `vault_since`     | `dir`, `since`            | notes modified after a Unix timestamp |
| `vault_links`     | `file`                    | a file's outbound links |
| `vault_graph`     | `dir`                     | the whole vault's link graph |
| `vault_doctor`    | `dir`                     | vault hygiene report: per-note size, age, distinct link degree, dangling links (wikilinks, embeds, relative `.md` links), TODO/FIXME markers, and maintenance flags (unreadable/oversized/stale/orphan); the summary carries `clean` |
| `vault_seed`      | `dir`                     | scaffold a memory vault ("brain") for the repo containing `dir`, additively; returns repo facts, a step-by-step fill plan, a `wire_snippet` for the host AGENTS.md, and the done-check (`vault_doctor` clean) |
| `vault_edit`      | `file`, `heading`, `op`, `text` | surgically edit a section (`op` = append/insert/replace/before), saved atomically; echoes the new section |
| `vault_set_frontmatter` | `file`, `key`, `value` | set a YAML frontmatter key, saved atomically |

Each tool's result is a text content block whose text is the same JSON the
corresponding `glance --…` command prints, so an agent can parse it directly.

## Protocol notes

- **Transport:** stdio, one JSON-RPC message per line (no embedded newlines).
- **Methods:** `initialize`, `tools/list`, `tools/call`, `ping`, and
  `notifications/*` (notifications get no response). `initialize` echoes the
  client's `protocolVersion`.
- **Errors:** standard JSON-RPC codes — `-32700` parse error, `-32601` method not
  found, `-32602` unknown tool.
- **Writes are surgical:** `vault_edit` / `vault_set_frontmatter` change a
  section or a frontmatter key in place (formatting preserved) and save
  atomically — the agent never rewrites a whole file.
