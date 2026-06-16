# glance, in the age of agents

This branch (`c-agent-features`) builds new functionality on top of the Go-parity
reader. The thesis: a Markdown reader today should serve **two readers at once —
the human at the terminal and the agent reading the same files.** Markdown is
already the lingua franca agents use for memory, docs, and context; glance turns
a folder of `.md` into something both can navigate.

## Pain points found (research, June 2026)

- Terminal readers (glow, mdcat, bat) render but don't *navigate*: no search,
  no following links between files, no sense of a vault. ([adamsdesk], [dev.to])
- What people love about Obsidian is exactly the connective tissue: `[[wikilinks]]`,
  **backlinks**, and the **graph view** — but it's a GUI Electron app. ([techtimes], [lindy])
- Agents increasingly treat Markdown as the ideal format and build *knowledge
  graphs* from it (GraphMD, markdown-KG "for humans and agents", LLM wikis,
  llms.txt). The missing piece is a fast, scriptable CLI that exposes a vault's
  structure. ([medium-graphmd], [dev.to-kg], [starmorph])

## What glance adds

1. **Wikilinks + cross-file navigation.** `[[note]]` resolves to `note.md` in the
   vault; Enter follows any internal link, with a back-stack to return. This is
   Obsidian's core, in the terminal.
2. **Backlinks.** A panel showing every file that links to the current one —
   discovered by scanning the vault.
3. **Agent-facing structured output (the headline).** Non-interactive subcommands
   that print JSON for tools and agents to consume:
   - `glance --outline FILE`  → the heading tree.
   - `glance --links FILE`    → outbound links (Markdown + wikilinks).
   - `glance --graph DIR`     → the whole vault as `{nodes, edges}`.
   An agent can call `glance --graph .` to understand how a doc set connects,
   then `glance --outline x.md` to navigate one file — no embeddings, no server,
   just structure.

Everything stays strictly in the terminal / stdout. Same renderer, same Doc.

### How the "vault" is defined (no init needed)

There is no `glance --init` and no index file — **the folder is the vault**, the
same as Obsidian. When you open a file, glance finds the vault root by walking up
to the nearest `.git` or `.obsidian` marker, falling back to the file's own
directory. From that root it scans **recursively**, so `[[wikilinks]]` resolve to
notes anywhere in the tree (including subfolders). `glance --graph DIR` takes the
root explicitly. Markdown links that contain a path resolve relative to the
current file; bare `[[names]]` resolve by name across the whole vault.

## Status — all implemented on this branch

- `[[wikilinks]]` render and are followable; Enter opens internal `.md` targets
  inside glance with a back-stack (`-` / `Ctrl-O`); external URLs open in the
  browser.
- `b` opens a backlinks panel (files in the folder that link to the current one).
- `glance --outline` / `--links` / `--graph` print JSON (see README). Unit-tested
  via `vault_test` and `agent_test`; the render path is ASan-clean with links.
- **Graph explorer (`Ctrl-G`)** — the human counterpart to `--graph`: a local
  graph view, the current note centred with its inbound links on the left and
  outbound on the right. Walk the vault by re-centring (`Space`). Built on the
  same `graph_build` (graph.c) the JSON export uses.

### Candidate next steps (not yet built)
- Inline image rendering (notcurses supports sixel/kitty/iterm protocols).
- An MCP server mode (`glance --mcp`) exposing search/outline/graph to agents.
- Quick-open (`Ctrl-P` fuzzy), tags (`#tag`), and YAML frontmatter.

## Sources

- [adamsdesk] https://www.adamsdesk.com/posts/linux-markdown-viewers/
- [dev.to] https://dev.to/dunkinfrunkin/i-built-a-markdown-pager-for-the-terminal-because-i-live-in-the-cli-and-nothing-else-worked-h95
- [techtimes] https://www.techtimes.com/articles/315717/20260407/why-use-obsidian-note-taking-graph-view-linked-notes-powerful-knowledge-management.htm
- [lindy] https://www.lindy.ai/blog/obsidian-review
- [medium-graphmd] https://medium.com/@mail_36332/introducing-graphmd-turning-markdown-documents-into-executable-knowledge-graphs-6925d936423f
- [dev.to-kg] https://dev.to/gimalay/markdown-knowledge-graph-for-humans-and-agents-43c4
- [starmorph] https://blog.starmorph.com/blog/karpathy-llm-wiki-knowledge-base-guide
