# Vault index

This folder is a tiny "vault" — a set of Markdown notes linked together, the way
Obsidian works. There is **no setup**: the folder *is* the vault. The `.obsidian`
marker here just tells glance where the vault root is, so links resolve across
subfolders.

Try it: put the cursor on a link below and press **Enter** to follow it. Press
**`-`** (or Ctrl-O) to come back, and **`b`** on any note to see what links to it.

## Start here

- [[Rendering]] — how glance turns Markdown into what you see
- [[Vault Mode]] — wikilinks, backlinks, and the link graph

Both of those live in the `notes/` subfolder, yet `[[Rendering]]` still resolves
— that is the recursive vault scan at work.

## For agents

From a shell, ask glance about this vault without opening it:

```sh
glance --graph testdata/vault     # the whole link graph as JSON
glance --outline testdata/vault/index.md
glance --links   testdata/vault/index.md
```
