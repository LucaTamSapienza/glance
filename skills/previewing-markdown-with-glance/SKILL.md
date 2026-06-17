---
name: previewing-markdown-with-glance
description: Use right after writing or editing a markdown file, or when the user asks to read/see/preview a document — proactively offer to render it with glance (inline glance-render preview, or the glance TUI for interactive reading). Skip for trivial one-line edits.
---

# Previewing markdown with glance

glance is the user's markdown viewer. When markdown is worth looking at,
**proactively offer to show it with glance** instead of leaving raw text.

## When to offer

- You just **wrote or substantially edited** a `.md` file (a README, a doc, a
  spec, notes). Offer a preview once it's saved.
- The user asks to **read / see / preview / "show me"** a markdown document.
- Skip it for trivial edits (a typo, one line) — use judgment, don't nag.

## Two ways to show it

glance has two sinks; pick by what the user needs:

1. **Inline preview (default offer)** — render it right in the session, framed
   so it's obviously a rendered document:
   ```bash
   f="FILE"
   printf '\n───── 📄 %s · rendered by glance ─────\n\n' "$(basename "$f")"
   glance-render "$f"          # add --theme NAME, -w COLS, or -l for light
   printf '\n─────────────────────── end ───────────────────────\n'
   ```
   Themed ANSI to stdout: headings, syntax-highlighted code, aligned tables.
   You can run this yourself; no terminal takeover needed.

   **Then signpost it.** Claude Code collapses long tool output, so the user can
   miss that a render is there. Begin your reply with a clear line such as:
   *"📄 Rendered `FILE` with glance below — expand the output (Ctrl+O) to read
   it."* Don't leave the user to discover it.

2. **Interactive read** — for scrolling, the table of contents, the theme
   picker, or following `[[wikilinks]]`, the user runs the full TUI themselves:
   > "To read it interactively: `glance FILE` (you can run it here with `!glance FILE`)."
   Don't try to launch the TUI from a tool call — it needs the user's terminal.

## First time

If `glance-render` / `glance` aren't found, point the user at the install steps
(clone + `make install`; deps `brew install md4c notcurses pkg-config`) and offer
the inline preview once it's available.

## The offer, in practice

After saving a doc: *"Want me to preview that with glance?"* — then on a yes run
the framed `glance-render` above, open your reply with the "📄 Rendered … —
expand (Ctrl+O)" signpost, and mention the interactive `glance FILE` option for a
longer read.
