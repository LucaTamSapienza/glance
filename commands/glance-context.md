---
description: Retrieve a token-cheap, budget-bounded context bundle from a markdown vault using glance (--context)
allowed-tools: Bash, Read
---

Answer a question *from a markdown vault* without reading whole files. Treat the
user's request in `$ARGUMENTS` as the query (and, if they named a vault folder,
the directory; otherwise use the current vault/working directory).

First make sure glance is installed, then retrieve the bundle:

```bash
command -v glance >/dev/null || { echo "glance not found — install it: clone https://github.com/LucaTamSapienza/glance and run 'make install' (needs 'brew install md4c notcurses pkg-config')"; exit 1; }
glance --context "$ARGUMENTS" . --budget 4000
```

The output is JSON: `{ query, budget_tokens, chunks, truncated, receipt }`.

- **`chunks`** is the retrieved context — note sections ranked by relevance (BM25 +
  a link-graph prior), some as full sections, some as cheaper abstracts. Answer the
  user's question **from these**, and cite sources as `note#heading`.
- **`truncated`** lists what didn't fit (with scores). If the answer needs one of
  those, pull it with `glance --section "FILE#Heading"` rather than re-running with
  a bigger budget.
- **`receipt`** shows how many tokens this used versus reading the whole vault —
  mention the saving; it's the point.

Adjust `--budget` to the answer's size, add `--semantic` for fuzzy/conceptual
queries, and pass an explicit vault directory if the notes live elsewhere.
