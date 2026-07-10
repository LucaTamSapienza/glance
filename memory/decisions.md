# Decisions

> Dated, newest first, each with the why. The *rules* these produced live in
> AGENTS.md; this note records why they exist. One `##` per decision —
> headings are glance's retrieval unit. Drop an entry once it stops
> informing anything.

## 2026-07-10 — --seed: the brain scaffold is a command; structure ≠ knowledge

`glance --seed` (MCP: `vault_seed`) gives any repo a brain: walk to the repo
root, additively write `memory/` — an index doubling as the protocol +
status/decisions/lessons/history with seed fill-markers — and print repo
facts, a fill plan, a `wire_snippet` for the host AGENTS.md, the done-check.
The split is deliberate: glance (no LLM) writes *structure*, the calling
agent writes *knowledge*, and `--doctor` going clean means "brain ready" — the
fill needs no new verification code. The templates are the generic instance
of this very vault, i.e. the one-shot instantiation of Karpathy's LLM-wiki
pattern. Named `--seed`, not `--init`, so the "vault is a folder" invariant
keeps its exact meaning: reading never needs setup, seeding plants plain
notes. With it, doctor's TODO-marker rule tightened to require a colon or
paren after the word, so prose that documents the protocol (or, as this
entry learned firsthand, quotes a marker) no longer self-flags.

## 2026-07-10 — The memory protocol got a mechanical half: --doctor

The protocol's rules (update triggers, the ~150-line cap, distill-don't-
append) were prose only — nothing checked them. Now `./glance --doctor DIR`
(MCP: `vault_doctor`) reports per-note lines, age, link degree, dangling
`[[wikilinks]]` and TODO markers, and flags oversized (>150 lines), stale
(>45 days) and orphaned notes. Facts live in doctor.c (pure, the 28th
suite); the thresholds are judgements and live in agent.c, documented in
`--help`. The update/fill/delete trigger table lives in memory/MEMORY.md;
AGENTS.md tells every agent to lint the vault after touching it and fix
what it flags.

## 2026-07-10 — Eddie's claim store: research to finish, not a brain swap

`feature/claim-store` (Eddie, 2026-06-27→07-02) tests a "claim store":
knowledge as content-addressed scoped claims (assert/supersede/retract,
conflict-flagging with provenance) instead of documents. Analyzed in full
2026-07-10 — verdict: **do not replace the vault with it.** (1) Its own
pre-registered verdict is NO-GO: Gate 1 (hot-node merge propagation —
exactly what an agent-memory vault is full of) was never measured; the
frozen TCP stress test (40 articles, SHA-256-locked criterion) is
unexecuted. (2) The replicated Gate 3 win (15–26pp fewer in-context
contradictions at 0.62–0.76× tokens; 3 runs, 2 domains, p<0.02) beats a
*stale flat pile* — a baseline glance never was: we read the current file,
and "distill, don't append" is already a hand-operated claim store; the
spec's own hard-skeptic baseline (flat RAG + query-time NLI) never ran.
(3) The pipeline needs an extraction LLM + a PyTorch NLI oracle — against
the local/lightweight ethos (the bge-m3 grounds). Salvage: Gate 3 as
evidence *for* current-state reads; conflict-flagging as a future additive
`--conflicts` lint; the pre-registration method as our benchmark template.
If pursued: run the hot-node test, then demand the bolt-on baseline.

## 2026-07-01 — Enhanced keyboard is opt-in; legacy stays the default

Option/Cmd+arrow chords are invisible in legacy keyboard mode on terminals
that collapse them to bare letters. `keyboard = enhanced` in the config (or
`GLANCE_KEYBOARD=enhanced`) keeps notcurses' kitty protocol active so those
chords carry real modifier bits; `glance --keys` honours the mode. Legacy
stays the default because the protocol leaked onto the shell at exit on
iTerm2 — mechanics and un-wedge recipe in [[lessons]] — and teardown now
clears the whole kitty stack (counted pop) in both modes. Flip the default
only after field testing on real terminals.

## 2026-07-01 — Docs reorganized: one source per fact + this memory vault

Six root-level prose docs (README, CLAUDE, AGENTS, STATUS, context,
AGENT_FEATURES) repeated the module map and status in 3–4 places each and
measurably drifted (AGENTS.md still said 23 suites and "proportional" cursor
sync long after both changed). Now: README = product, AGENTS.md = the one
working guide (CLAUDE.md just imports it), memory/ = living state, docs/ =
DESIGN + MCP + specs/ + archive/. STATUS.md, context.md and AGENT_FEATURES.md
were absorbed and deleted; HANDOFF and REVIEW archived. Heads-up:
feat/semantic-minilm still edits the deleted STATUS.md/context.md and will
conflict at merge — fold its deltas into [[status]].

## 2026-07-01 — spike/ and third_party/ are local-only on main

~620 MB of llama.cpp checkouts + gguf models used by the semantic work, now
gitignored so `git status` stays honest and a `git clean -fd` can't destroy
them. feat/semantic-minilm tracks `third_party/llama.cpp` as a submodule —
ignoring the path on main is harmless because gitignore never affects
tracked paths. (Heads-up 2026-07-10: `feature/claim-store` commits its own
`spike/` files; merging it would put tracked files under an ignored path —
harmless for those, but anything *new* under `spike/` would stop showing in
`git status`. Revisit the ignore line then.)

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
Order of work: app complete A→Z first, then the eval/benchmark. (Note
2026-07-10: Eddie's claim-store spec declares the compression thesis dead
and pitches consistency instead — reconcile before either paper is written;
they are complementary: consistency is *what you read*, the budget frontier
*what it costs*.)

## 2026-06-19 — MiniLM semantic tier: GO; fp16; cache mandatory

On-device spike (`spike/minilm`, local-only): ~150–180 ms model load +
~24 ms/chunk on CPU via llama.cpp, ~4× faster cache builds on Metal, no
thermal throttle. Ship **fp16** (44 MB; quantization = complexity for no
gain), a persistent `.glance/` vector cache (re-embedding the vault per
query is a non-starter), ~5 threads. Implemented on feat/semantic-minilm.

## 2026-06-19 — WYSIWYG Live mode parked after trial

Luca tried slice 1 (`feat/wysiwyg-inline`) and wasn't convinced: the open
UX problem is the layout "breathing" (the raw active line's height differs
from its rendered height, shifting everything below). Parked, no PR — don't
merge or resume unless he asks.

## 2026-06-19 — No in-TUI AI edit overlay

Built (feat/ai-edit), disliked, deleted and fully reverted. glance's AI
story is the *agent layer* (CLI + MCP), not chat inside the editor. Do not
rebuild it.

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
sinks makes every consumer a direct read of one model; notcurses replaces
bubbletea. Go survives at the `go-final` tag.

