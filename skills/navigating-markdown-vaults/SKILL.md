---
name: navigating-markdown-vaults
description: Use when answering how a set of markdown notes connects, finding a note's backlinks, mapping a vault, or surveying a document's structure — drive glance's outline/links/graph exports instead of grepping for links by hand.
---

# Navigating markdown vaults with glance

glance parses Markdown (including `[[wikilinks]]`) and emits **JSON** built for
agents. Prefer it over grepping: it resolves wikilinks by stem across the whole
tree and gives you the real link graph, not a regex approximation.

## When to use

- "How do these notes connect?" / "What links to X?" / "Find orphans" → the graph.
- "What does this file link to?" → the links export.
- "What's the structure of this doc?" / "Jump to a section" → the outline.

## Commands

| Need | Command | Output |
|------|---------|--------|
| Heading tree of a file | `glance --outline FILE` | `[{level,title,line}]` |
| Outbound links of a file | `glance --links FILE` | `[{target,wiki}]` |
| Whole-vault link graph | `glance --graph DIR` | `{nodes,edges}` |

(The slash commands `/glance-outline`, `/glance-links`, `/glance-graph` wrap
these.)

## How to work

1. **Check it's installed:** `command -v glance`. If missing, point the user at
   the install steps (clone + `make install`, deps `brew install md4c notcurses
   pkg-config`) and stop.
2. **Run the right export** and parse the JSON — treat it as ground truth.
3. For "how does this connect" questions, **start with `glance --graph .`** to
   get hubs, orphans, and clusters before reasoning about content.
4. A note's **backlinks** = the edges in the graph whose target is that note.

Don't reimplement link parsing — that's exactly what glance already does.
