# Decisions

> Dated, newest first, each with the why. The *rules* these produced live in
> AGENTS.md; this note records why they exist. One `##` per decision —
> headings are glance's retrieval unit. Drop an entry once it stops
> informing anything.

## 2026-07-01 — Enhanced keyboard is opt-in; legacy stays the default

Option/Cmd+arrow chords are invisible in legacy keyboard mode on terminals
that collapse them to bare letters (see [[lessons]]). The `keyboard =
enhanced` config key (or `GLANCE_KEYBOARD=enhanced`) now keeps notcurses'
kitty protocol active for the session, so those chords carry real modifier
bits and the existing word-jump / line start-end bindings light up; `glance
--keys` honours the mode. Legacy remains the default because the protocol
leaks sequences onto the shell at exit on iTerm2 — reproduced live
2026-07-01, see [[lessons]] — so teardown now clears the whole kitty stack
with a counted pop (`CSI < 64 u`) in both modes. Flip the default only after
field testing on real terminals.

## 2026-07-01 — Docs reorganized: one source per fact + this memory vault

Six root-level prose docs (README, CLAUDE, AGENTS, STATUS, context,
AGENT_FEATURES) repeated the module map and status in 3–4 places each and
measurably drifted (AGENTS.md still said 23 suites and "proportional" cursor
sync long after both changed). Now: README = product, AGENTS.md = the one
working guide (CLAUDE.md just imports it), memory/ = living state, docs/ =
DESIGN + MCP + specs/ + archive/. STATUS.md, context.md and AGENT_FEATURES.md
were absorbed and deleted; HANDOFF and REVIEW archived. Heads-up: branches
that still touch the deleted files (feat/semantic-minilm edits
STATUS.md/context.md) will conflict at merge — resolve by folding their
deltas into [[status]].

## 2026-07-01 — spike/ and third_party/ are local-only on main

~620 MB of llama.cpp checkouts + gguf models used by the semantic work, now
gitignored so `git status` stays honest and a `git clean -fd` can't destroy
them. feat/semantic-minilm tracks `third_party/llama.cpp` as a submodule —
ignoring the path on main is harmless because gitignore never affects
tracked paths.

## 2026-07-01 — make test probes ASan instead of assuming it

asan's runtime deadlocks at init on macOS 26 (see [[lessons]]), so a
hard-coded `-fsanitize=address` hung the suite at 100% CPU — it looked like
an infinite build loop. The recipe now runs a trivial asan binary under a
5 s watchdog and drops to UBSan-only when it can't start: full ASan stays on
CI and healthy machines, and the suites always run.

## 2026-07-01 — The TUI owns SIGWINCH (self-pipe), not notcurses

Window resize and Cmd +/- font zoom left the reader stale because notcurses'
`NCKEY_RESIZE` never reached glance's external poll loop (see [[lessons]]).
glance now sets `NCOPTION_NO_WINCH_SIGHANDLER`, installs its own SIGWINCH
handler writing to a self-pipe polled beside input, and on wake calls
`notcurses_refresh` + one reflow (bursts coalesce). Don't hand resize
handling back to notcurses.

## 2026-06-25 — Paper direction: the contribution is accuracy-per-token

Luca is steering the agent layer toward a paper ("budget-aware agent memory
over a plaintext vault"). The defensible claim is the token-cost/quality
Pareto frontier plus relational (graph) retrieval — "markdown + embedder"
alone reads as "just RAG" to reviewers. Decided with it: swap the embedder
to **EmbeddingGemma-300M @ 256-dim** (Matryoshka truncation) because
MiniLM-L6 is English-only and degrades silently on Italian/mixed vaults
(bge-m3 rejected: 568M/1024-dim breaks the lightweight ethos); an opt-in
`--rerank` using **jina-reranker-v2** (avoid qwen3-reranker — llama.cpp
rerank bug); cache refresh = content-hash as truth, mtime as the fast gate.
Order of work: app complete A→Z first, then the eval/benchmark.

## 2026-06-19 — MiniLM semantic tier: GO; fp16; cache mandatory

On-device spike (`spike/minilm`, local-only): all-MiniLM-L6-v2 via llama.cpp
costs ~150–180 ms model load + ~24 ms/chunk on CPU, ~4× faster cache builds
on Metal, no thermal throttle. Ship **fp16** (44 MB model; quantization =
complexity for no gain), a persistent `.glance/` vector cache (re-embedding
the vault per query is a non-starter), ~5 threads not 10. Implemented on
feat/semantic-minilm.

## 2026-06-19 — WYSIWYG Live mode parked after trial

Luca tried slice 1 (`feat/wysiwyg-inline`) and wasn't convinced by the feel;
the open UX problem is the layout "breathing" (the raw active line's height
differs from its rendered height, shifting everything below). Parked on its
branch, no PR — don't merge or resume unless he asks.

## 2026-06-19 — No in-TUI AI edit overlay

An AI-section-edit overlay (feat/ai-edit) was built and Luca disliked it;
the branch was deleted and fully reverted. glance's AI story is the *agent
layer* (CLI + MCP), not chat inside the editor. Do not rebuild it.

## 2026-06-17 — The pivot: the agent-native memory layer is the product

A terminal reader alone doesn't differentiate; the durable edge is serving
the human and the agent over the same vault — bounded reads, budgeted
retrieval with a token receipt, surgical writes, MCP. Specced in
docs/DESIGN.md and shipped as four stacked milestones (M1–M4, PRs #9–#14),
with MCP tools reusing the exact CLI exports so the two surfaces can't drift.

## 2026-06-16 — PR workflow on a protected main; sole-author commits

Direct pushes to main are blocked (ruleset `protect-main`); every change
lands by PR. Commits credit Luca only — the history was once rewritten
(filter-branch) to strip assistant co-author trailers; never add them.

## 2026-06-15 — Rewrite in C and own the renderer

Most of the Go code existed to fight glamour's opacity (marker paragraphs to
preserve blanks, ANSI-stripping to search). md4c → our structured `Doc` →
sinks makes search, the TOC, and the agent projections direct reads of one
model; notcurses replaces bubbletea. Go was preserved at the `go-final` tag
and then removed from the tree.
