# Vault Mode

A "vault" is just a folder of Markdown files connected by links. glance reads it
the way Obsidian does — no database, no index, no `init` step.

- `[[wikilinks]]` resolve by name across the whole folder tree.
- **Backlinks** (`b`) list every note that links to the current one.
- The **link graph** is available to tools and agents as JSON: `glance --graph`.

This note links back to [[index]] and across to [[Rendering]]. Follow either with
Enter, then press `-` to return here.
