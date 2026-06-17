---
description: Show a markdown file's heading tree using glance (--outline)
allowed-tools: Bash, Read
---

Show the heading structure of the markdown file in `$ARGUMENTS` (if empty, use
the markdown file currently in context; if there's none, ask which file).

First make sure glance is installed, then run its outline export (add `--depth N`
to bound the levels and `--abstract` for a one-line summary per heading):

```bash
command -v glance >/dev/null || { echo "glance not found — install it: clone https://github.com/LucaTamSapienza/glance and run 'make install' (needs 'brew install md4c notcurses pkg-config')"; exit 1; }
glance --outline "$ARGUMENTS"
```

The output is JSON — an array of `{ "level", "title", "line" }` (plus `abstract`
with `--abstract`). Treat it as ground truth (glance's own parse, not a guess).
Present it as a clean indented heading tree, and offer to pull a specific section
cheaply with `glance --section "$ARGUMENTS#Heading"` or to open the file
interactively (`glance "$ARGUMENTS"`).
