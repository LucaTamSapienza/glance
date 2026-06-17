# glance, in the age of agents

The thesis: a Markdown tool today should serve **two readers at once — the human
at the terminal and the agent reading the same files.** Markdown is already the
lingua franca agents use for memory, docs, and context; glance turns a folder of
`.md` into something both can navigate *and maintain*. This document is the
rationale for the agent-side; the full design is in [docs/DESIGN.md](docs/DESIGN.md).

## The problem (agent ↔ vault, today)

An agent working against an Obsidian-style vault has only one move: *read the raw
files*. That is expensive and blind:

- **Whole-file reads waste tokens** — loading a 2,000-word note to use one paragraph.
- **Discovery is blind** — to find the right note it reads ten; full-text search
  returns lines, not meaning.
- **Structure is invisible** — to learn how the brain connects, it must crawl
  `[[wikilinks]]` one by one.
- **Writing is terrifying** — to change a note it rewrites the whole file (costly
  and risky) or avoids writing at all.

Terminal readers (glow, mdcat, bat) render but don't *navigate*; what people love
about Obsidian — wikilinks, backlinks, the graph — is a GUI Electron app. The
missing piece is a fast, local, scriptable layer that exposes a vault's structure
to an agent and lets it read and write for a fraction of the tokens.

## Why this is not "just a Skill"

A prompt (a Claude Skill) tells the agent *how to behave*, but the agent still
uses its own tools, burns tokens in its own context window, and has no graph. glance
is an **engine**: it indexes, ranks, slices, and edits in native C, *outside* the
model, and works for **any** agent — Claude, Cursor, the SDK, cron, CI — not just
one. The Skill is the steering wheel; glance is the motor.

## What glance adds (all shipped)

Every read is **bounded** so it stays token-cheap; retrieval carries a **token
receipt** (tokens used vs a naive whole-file read).

1. **Budget-bounded retrieval — `glance --context "Q" DIR --budget N` (the headline).**
   Assembles the optimal bundle under a token ceiling: note sections ranked by
   **BM25 fused with a link-graph prior**, selected with diversity and a
   coarse-to-fine projection (full section, or its abstract if it won't fit), plus
   a **truncation manifest** so the agent knows what was dropped and can follow up.
   Returns `{query, budget_tokens, chunks, truncated, receipt}`. Lexical + graph by
   default; `--semantic` fuses an embedding score (dependency-free embedder ships;
   a MiniLM-class encoder is a drop-in behind the same interface).
2. **Section-addressed reads — `glance --section "FILE#Heading"`.** One heading's
   subtree (matched by text or slug) plus a receipt — read 300 tokens instead of
   3,000.
3. **Bounded structure — `glance --outline FILE --depth N --abstract`,
   `--neighbors`, `--backlinks --context`, `--since`.** The heading tree (with
   per-heading abstracts), the link-graph neighbourhood, who links here (and the
   citing line), and the "what changed?" delta — each depth/scope-bounded.
4. **Surgical writes — `glance --edit FILE append|insert|replace "Heading" "text"`,
   `--set-frontmatter`.** The agent declares intent + location; glance finds the
   section, preserves all other formatting (headings inside fenced code are
   ignored), and writes atomically. The agent *maintains* the vault instead of
   rewriting whole files.
5. **MCP server — `glance mcp`.** Exposes all of the above as native
   [MCP](https://modelcontextprotocol.io) tools over stdio JSON-RPC 2.0 (Claude
   Desktop, Cursor, the Agent SDK). See [docs/MCP.md](docs/MCP.md).

Everything stays in the terminal / on disk. Same renderer, same `Doc`, no server
process to keep running, no embeddings required, nothing leaves your machine.

## How the "vault" is defined (no init needed)

**The folder is the vault**, like Obsidian. glance finds the root by walking up to
the nearest `.git` or `.obsidian` marker (else the file's directory) and scans it
**recursively**, so `[[names]]` resolve by stem across the whole tree. The
human-facing counterpart of `--graph` is the in-terminal **graph explorer**
(`Ctrl-G`), built on the same `graph.c`.

## The moat

- **Local graph + structure, no mandatory embeddings** — privacy, zero per-query
  cost, deterministic, citable (`note#heading`).
- **Owned renderer** — projects a note at any granularity (full / section /
  abstract); a glamour black box cannot.
- **Safe writes** — the retention feature: an agent that keeps the brain coherent.

The acceptance test for every feature: *would an agent, given the choice, pick
glance because it is cheaper **and** the output is better than not using it?*

## Sources

- [adamsdesk] https://www.adamsdesk.com/posts/linux-markdown-viewers/
- [dev.to] https://dev.to/dunkinfrunkin/i-built-a-markdown-pager-for-the-terminal-because-i-live-in-the-cli-and-nothing-else-worked-h95
- [techtimes] https://www.techtimes.com/articles/315717/20260407/why-use-obsidian-note-taking-graph-view-linked-notes-powerful-knowledge-management.htm
- [medium-graphmd] https://medium.com/@mail_36332/introducing-graphmd-turning-markdown-documents-into-executable-knowledge-graphs-6925d936423f
- [dev.to-kg] https://dev.to/gimalay/markdown-knowledge-graph-for-humans-and-agents-43c4
- [starmorph] https://blog.starmorph.com/blog/karpathy-llm-wiki-knowledge-base-guide
