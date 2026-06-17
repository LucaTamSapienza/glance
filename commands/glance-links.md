---
description: List a markdown file's outbound links and wikilinks using glance (--links)
allowed-tools: Bash, Read
---

List the outbound links of the markdown file in `$ARGUMENTS` (if empty, use the
markdown file in context; otherwise ask which file).

```bash
command -v glance >/dev/null || { echo "glance not found — install it: clone https://github.com/LucaTamSapienza/glance and run 'make install' (needs 'brew install md4c notcurses pkg-config')"; exit 1; }
glance --links "$ARGUMENTS"
```

The output is JSON — an array of `{ "target", "wiki" }`, where `wiki` is true for
`[[wikilinks]]`. Summarize for the user: group plain URLs vs `[[wikilinks]]`, and
call out anything that looks unresolved or suspicious. Offer to follow a link
(open the target with glance) if it's a local note.
