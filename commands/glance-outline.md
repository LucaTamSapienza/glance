---
description: Show a markdown file's heading tree using glance (--outline)
allowed-tools: Bash, Read
---

Show the heading structure of the markdown file in `$ARGUMENTS` (if empty, use
the markdown file currently in context; if there's none, ask which file).

First make sure glance is installed, then run its outline export:

```bash
command -v glance >/dev/null || { echo "glance not found — install it: clone https://github.com/LucaTamSapienza/glance and run 'make install' (needs 'brew install md4c notcurses pkg-config')"; exit 1; }
glance --outline "$ARGUMENTS"
```

The output is JSON — an array of `{ "level", "title", "line" }`. Treat it as
ground truth (it's glance's own parse, not a guess). Present it to the user as a
clean indented heading tree, and offer to open the file at a chosen section
(`glance "$ARGUMENTS"` jumps interactively) or to expand a specific part.
