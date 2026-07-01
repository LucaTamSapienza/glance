# History

> How the project got here — the arc in milestones, distilled from the git
> log and the PRs. For the *why* behind each turn, see [[decisions]].
> Chronological, oldest first; one `##` per milestone.

## 2026-05-20 — Born as a Go TUI

bubbletea + glamour/goldmark. The first weeks were bug-fix batches
(code-fence completion, preserved blank lines, `:q` discipline, code-block
colors) and editor polish (word jumps, single-Esc exit, a 1:1
source↔rendered row map to sync reader and editor).

## 2026-06-14 — Tolerant Markdown + the Go releases

Setext neutralization, space-tolerant bold, the reader↔editor cursor-offset
fix; the module made installable; `v0.1.0`/`v0.1.1` tagged and released
(Go-era). Clipboard yank via vi-style visual select.

## 2026-06-15 — The C rewrite ("glance-c")

Own renderer: md4c → a structured `Doc` of styled runs → sinks (ANSI string,
notcurses cells). Every Go feature re-ported in slices (editor, search, TOC,
atomic save, kqueue reload, Split, help, yank, links, bracket pairing), plus
what Go never had: vault navigation (`[[wikilinks]]`, backlinks, the Ctrl-G
graph explorer) and the first agent JSON exports
(`--outline`/`--links`/`--graph`).

## 2026-06-16 — Migration + the four gaps

The C app promoted to the repo root; Go preserved at the `go-final` tag,
then removed. Same day: syntax highlighting, bordered aligned tables,
source-line cursor sync, inline images (pixel blit), clipboard-image paste
(Ctrl-V). Security/stability audit (AppleScript-injection fix,
symlink-cycle guard) → PR #1. Default branch renamed master → main; the
PR-based workflow starts.

## 2026-06-17 — Reader/editor maturity

Exact offset-based cursor sync (PR #4), heading chips (PR #5), editor
soft-wrap (PR #6), legend sidebar + trackpad scroll + the theme engine + the
Claude Code plugin (PRs #7–#8).

## 2026-06-18 — The agent-native memory layer (the pivot)

DESIGN.md, then four stacked milestones: M1 bounded reads + BM25 budgeted
retrieval (PR #9), M2 MCP server (PR #14 — the original #10 was killed by a
stacked-PR gotcha, see [[lessons]]), M3 semantic-fusion infrastructure
(PR #11), M4 surgical write API (PR #12). An adversarial review hardened the
untrusted-input boundary (PR #13 → docs/archive/REVIEW.md); all docs
rewritten around the two faces (PR #15).

## 2026-06-19 — Feature wave

NBSP TOC fix (PR #16); HTML/PDF export, cross-session live-sync, the Ctrl-P
fuzzy switcher, and four new themes — built on parallel branches, integrated
and senior-reviewed (PRs #17–#20).

## 2026-06-19 — Two bets branched

WYSIWYG Live mode slice 1 (`feat/wysiwyg-inline`, parked after Luca tried
it) and the MiniLM on-device spike (GO verdict) that became
`feat/semantic-minilm`.

## 2026-06-25 — The real semantic tier (on its branch)

llama.cpp as a submodule with a static link, the MiniLM embedder behind the
`Embedder` seam, the persistent `.glance/` cache, model
download-on-first-use, k-hop graph-expansion retrieval. Paper direction set
([[decisions]]).

## 2026-07-01 — Environment fixes + this memory

The `make test` asan probe and the SIGWINCH self-pipe resize fix (PR #21);
docs reorganized to one source per fact and this memory vault created
([[status]]).
