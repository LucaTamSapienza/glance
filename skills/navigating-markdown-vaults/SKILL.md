---
name: navigating-markdown-vaults
description: Use when reading, searching, retrieving context from, or editing a folder of Markdown notes — drive glance's agent-memory commands (context/section/outline/neighbors/backlinks/edit) instead of loading whole files or grepping by hand. glance gives token-cheap, structure-aware reads with a token receipt, the real link graph, and safe surgical edits.
---

# Navigating & maintaining markdown vaults with glance

glance is an **agent-native memory layer** over a folder of `.md` files: it reads
the vault for a fraction of the tokens a raw read costs, resolves `[[wikilinks]]`
by stem across the whole tree, and writes back surgically. Prefer it over loading
whole files or grepping — and check the **token receipt** it prints to see how
much you saved.

## When to use

- "How do we handle X?" / "What do the notes say about Y?" → **`--context`** (the
  budgeted retrieval bundle — start here for any answer-from-the-vault question).
- "Show me just the *Decisions* section of note Z" → **`--section`**.
- "What's the structure of this doc?" → **`--outline`** (depth-bounded, optional
  abstracts).
- "How does this connect?" / "What links to X?" → **`--neighbors`** / **`--backlinks`**.
- "What changed since yesterday?" → **`--since`**.
- "Add this / update that note" → **`--edit`** / **`--set-frontmatter`** (never
  rewrite a whole file by hand).

## Commands (JSON on stdout)

| Need | Command |
|------|---------|
| Best context for a query, under a token budget | `glance --context "Q" DIR --budget 4000` |
| One heading's subtree (+ receipt) | `glance --section "FILE#Heading"` |
| Heading tree, bounded | `glance --outline FILE --depth 2 --abstract` |
| Link-graph neighbourhood | `glance --neighbors "Note" DIR --depth 2` |
| Who links here (+ the citing line) | `glance --backlinks "Note" DIR --context` |
| What changed | `glance --since <unix_ts> DIR` |
| A file's outbound links / the whole graph | `glance --links FILE` · `glance --graph DIR` |
| Edit a section (atomic) | `glance --edit FILE append\|insert\|replace "Heading" "text"` |
| Set a frontmatter key | `glance --set-frontmatter FILE KEY VALUE` |

`--context` returns `{query, budget_tokens, chunks, truncated, receipt}`: `chunks`
is what fits (some as full sections, some as cheaper abstracts), `truncated` is
the manifest of what was left out with scores — follow up on a dropped item with
`--section` instead of raising the budget blindly. Add `--semantic` to fuse an
embedding score for fuzzy matches.

## How to work

1. **Check it's installed:** `command -v glance`. If missing, point the user at the
   install steps (`brew install md4c notcurses pkg-config`, clone, `make install`)
   and stop.
2. **Answer questions with `--context`** first; only fall back to `--section` /
   whole-file reads when you need a specific exact slice. Cite `note#heading`.
3. **For "how does this connect"**, use `--graph` / `--neighbors` for hubs,
   orphans, and clusters before reasoning about content.
4. **To change a note, use `--edit`** — declare the heading and the text; glance
   splices it in and saves atomically. Don't reconstruct the file yourself.
5. **Run commands synchronously** (never backgrounded), and parse the JSON as
   ground truth.

For a persistent integration (Claude Desktop, Cursor, the SDK), `glance mcp`
exposes all of the above as native MCP tools — see `docs/MCP.md`.
