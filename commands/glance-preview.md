---
description: Render a markdown file beautifully in the session using glance-render
allowed-tools: Bash, Read
---

Show a rendered, themed preview of the markdown file in `$ARGUMENTS` (if empty,
use the markdown file in context; otherwise ask which file) right here in the
session:

```bash
command -v glance-render >/dev/null || { echo "glance-render not found — install glance: clone https://github.com/LucaTamSapienza/glance and run 'make install' (needs 'brew install md4c notcurses pkg-config')"; exit 1; }
glance-render "$ARGUMENTS"
```

`glance-render` prints styled ANSI to stdout, so the user sees the rendered
markdown inline (headings, syntax-highlighted code, aligned tables). Add
`-w <cols>` to set width, `--theme <name>` for a theme, or `-l` for light.
Afterward, mention they can read it interactively (scroll, table of contents,
follow `[[wikilinks]]`) by running **`glance "$ARGUMENTS"`** in their terminal.
