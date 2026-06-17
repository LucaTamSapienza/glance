---
description: Map how a markdown vault links together using glance (--graph)
allowed-tools: Bash, Read
---

Map the link structure of the vault directory in `$ARGUMENTS` (if empty, use the
current directory `.`).

```bash
command -v glance >/dev/null || { echo "glance not found — install it: clone https://github.com/LucaTamSapienza/glance and run 'make install' (needs 'brew install md4c notcurses pkg-config')"; exit 1; }
glance --graph "${ARGUMENTS:-.}"
```

The output is JSON — `{ "nodes": [...], "edges": [...] }` for the whole vault.
Use it as the real link structure (don't grep for links yourself). Report the
**most-linked notes** (hubs), any **orphans** (no edges in or out), and obvious
**clusters**. This is the right first step before answering any question about
how a set of notes connects. Offer to open a hub note with glance.
