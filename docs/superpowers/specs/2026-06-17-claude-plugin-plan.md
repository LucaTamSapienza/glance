# glance as a Claude Code plugin — plan

> **Status:** Scaffolded & validated 2026-06-17 (branch `feature/legend`).
> Read this in the tool it describes: `glance docs/superpowers/specs/2026-06-17-claude-plugin-plan.md`

## The idea in two halves

1. **Claude *uses* glance** — its agent exports (`--outline`, `--links`,
   `--graph`) let an agent understand the shape of a markdown vault the way a
   human uses `[[wikilinks]]` and the graph.
2. **glance is *your* viewer, inside the session** — Claude proactively offers
   to render markdown with glance whenever it writes a doc or you want to read
   one.

No new rendering work either way: the plugin wraps output glance **already
produces** (the JSON exports and the ANSI renderer). The hard parts are done.

## glance as your in-session markdown viewer

glance has **two sinks**, which map cleanly to two situations:

- **`glance-render`** prints themed ANSI to **stdout** — so when *Claude* runs
  it, the rendered, syntax-highlighted, themed markdown shows up **right in your
  session**. No TTY needed; Claude does this itself for a quick visual.
- **`glance` (the TUI)** is for *interactive* reading — scroll, TOC, the theme
  picker, following wikilinks. Claude can't take over your terminal, so it
  **proposes the command** (`glance file.md`, runnable via the `!` prefix) when
  you want to actually navigate.

**How it auto-surfaces (skill-driven, chosen):** a skill,
`previewing-markdown-with-glance`, gives Claude strong "use when" triggers — when
it just wrote/edited a markdown file, or you ask to see a doc, it *offers*: an
inline `glance-render` preview, or the `glance` TUI for a real read. It uses
judgment (not every trivial one-line edit), and it touches **no settings.json** —
purely Claude being proactive. (A guaranteed PostToolUse hook is a possible
later opt-in via a `/glance-setup` command, but not in v1.)

## Why glance is a good fit

| Capability            | Already in glance        | Plugin surface              |
|-----------------------|--------------------------|-----------------------------|
| Heading tree          | `glance --outline f.md`  | `/glance-outline`           |
| Outbound links        | `glance --links f.md`    | `/glance-links`             |
| Whole-vault graph     | `glance --graph ./vault` | `/glance-graph`             |
| Rendered preview      | `glance-render f.md`     | skill guidance              |

Each command is a few lines: run the binary, hand Claude the JSON, let it reason.

## Structure (the repo *becomes* the plugin)

```
glance/
  .claude-plugin/
    plugin.json          # name, version, command list
    marketplace.json     # so others can `marketplace add` this repo
  commands/
    glance-outline.md    # /glance-outline <file>
    glance-links.md      # /glance-links <file>
    glance-graph.md      # /glance-graph <dir>
    glance-preview.md    # /glance-preview <file>  (inline glance-render)
  skills/
    navigating-markdown-vaults/SKILL.md       # Claude *uses* glance's exports
    previewing-markdown-with-glance/SKILL.md  # Claude *offers* glance to you
  src/ ...               # unchanged
```

## plugin.json (sketch)

```json
{
  "name": "glance",
  "description": "Navigate and understand markdown vaults with glance's JSON exports",
  "version": "0.1.2",
  "commands": [
    "./commands/glance-outline.md",
    "./commands/glance-links.md",
    "./commands/glance-graph.md",
    "./commands/glance-preview.md"
  ],
  "license": "MIT"
}
```

Skills in `skills/` are auto-discovered; they don't need listing here.

## Commands (each is one `.md` with frontmatter + instructions)

- **`/glance-outline <file>`** — run `glance --outline <file>`, summarize the
  heading tree, offer to jump to a section.
- **`/glance-links <file>`** — run `glance --links <file>`, list outbound links
  and flag broken `[[wikilinks]]`.
- **`/glance-graph <dir>`** — run `glance --graph <dir>`, report clusters,
  orphans, and the most-linked notes.
- **`/glance-preview <file>`** — run `glance-render <file>` and show the themed,
  rendered markdown inline; suggest `glance <file>` for an interactive read.

Each declares `allowed-tools: Bash, Read` and treats the output as ground truth.

## The two skills

- **`navigating-markdown-vaults`** — Claude *uses* glance. When to reach for it
  instead of grepping: "before answering how a set of notes connects, call
  `glance --graph .` for the real link structure." Covers a note's [[backlinks]]
  via the graph and following wikilinks across folders.
- **`previewing-markdown-with-glance`** — Claude *offers* glance to you. Triggers
  when it just wrote/edited a markdown file or you ask to read one: propose an
  inline `glance-render` preview, or `glance <file>` for interactive reading.
  Judgment-based (skip trivial edits), no config changes.

## Prerequisite & how it ties to the install work

The commands assume `glance` is on `PATH` — which is exactly what the new
`make install` (or a future Homebrew tap) provides. The skill's first step
verifies `command -v glance` and points to install docs if missing.

## Distribution

1. `marketplace.json` at repo root lists this repo as a plugin source.
2. A user runs `claude plugin marketplace add LucaTamSapienza/glance` then
   installs `glance`.
3. Commands/skill load automatically; they shell out to the user's installed
   `glance` binary.

## Phased build

1. **Scaffold** `.claude-plugin/plugin.json` + the four `commands/*.md`
   (outline, links, graph, preview).
2. **Author both skills** — `navigating-markdown-vaults` (Claude uses glance) and
   `previewing-markdown-with-glance` (Claude proactively offers it to you), each
   with clear "use when" triggers.
3. **Validate locally** — install the plugin from the local path; run each
   command against `testdata/vault/`; confirm a fresh session has Claude *offer*
   a preview after writing a markdown file.
4. **marketplace.json** + a README "Use with Claude" section.
5. (Later) the opt-in PostToolUse hook via `/glance-setup`; richer tools; or an
   MCP server.

## Decisions (locked)

- **Repo root is the plugin** — manifest at `.claude-plugin/`, so
  `marketplace add LucaTamSapienza/glance` just works.
- **Separate `/glance-*` commands** (clearer than one overloaded `/glance`).
- **Read-only, skill-driven proactivity, no settings.json changes** in v1.

## Non-goals (v1)

No MCP server, no write/edit commands, no bundling the binary (uses the user's
installed `glance`), no auto-firing hook (skill-driven offers only).
