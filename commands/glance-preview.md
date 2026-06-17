---
description: Render a markdown file beautifully in the session using glance-render
allowed-tools: Bash, Read
---

Show a rendered, themed preview of the markdown file in `$ARGUMENTS` (if empty,
use the markdown file in context; otherwise ask which file) right here in the
session, clearly framed so it's obvious it's a rendered document:

```bash
command -v glance-render >/dev/null || { echo "glance-render not found — install glance: clone https://github.com/LucaTamSapienza/glance and run 'make install' (needs 'brew install md4c notcurses pkg-config')"; exit 1; }
f="$ARGUMENTS"
printf '\n───── 📄 %s · rendered by glance ─────\n\n' "$(basename "$f")"
glance-render "$f"
printf '\n─────────────────────── end ───────────────────────\n'
```

`glance-render` prints styled ANSI to stdout, so the user sees the rendered
markdown inline (headings, syntax-highlighted code, aligned tables); the banner
makes it unmistakable. Add `-w <cols>` for width, `--theme <name>` for a theme,
or `-l` for light.

**After running it, signpost clearly** — Claude Code collapses long tool output,
so the user may not realize the render is there. Begin your reply with a line
like: *"📄 Rendered `<file>` with glance below — expand the output (Ctrl+O) to
read it."* Then mention they can read it interactively (scroll, table of
contents, follow `[[wikilinks]]`) by running **`glance "<file>"`** in their
terminal.
