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

## Sources

- [adamsdesk] https://www.adamsdesk.com/posts/linux-markdown-viewers/
- [dev.to] https://dev.to/dunkinfrunkin/i-built-a-markdown-pager-for-the-terminal-because-i-live-in-the-cli-and-nothing-else-worked-h95
- [techtimes] https://www.techtimes.com/articles/315717/20260407/why-use-obsidian-note-taking-graph-view-linked-notes-powerful-knowledge-management.htm
- [lindy] https://www.lindy.ai/blog/obsidian-review
- [medium-graphmd] https://medium.com/@mail_36332/introducing-graphmd-turning-markdown-documents-into-executable-knowledge-graphs-6925d936423f
- [dev.to-kg] https://dev.to/gimalay/markdown-knowledge-graph-for-humans-and-agents-43c4
- [starmorph] https://blog.starmorph.com/blog/karpathy-llm-wiki-knowledge-base-guide
