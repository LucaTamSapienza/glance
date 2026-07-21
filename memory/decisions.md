# Decisions

> Dated, newest first, each with the why. The *rules* these produced live in
> AGENTS.md; this note records why they exist. One `##` per decision —
> headings are glance's retrieval unit. Drop an entry once it stops
> informing anything.

## 2026-07-22 — Memory-first steering; retrieval budget default 4000

A 12-question / 10-edit eval vs a no-glance baseline (tests/eval/ →
docs/archive/EVAL-2026-07-22.jsonl) set the defaults. Budget 2000 truncated one answer in
four (the score-greedy planner admits big low-density chunks first); 4000
fits every answer at ~35% saved; --semantic rescues reworded and Italian
queries; updates land 9/9 at −85% tokens. Policy shipped in AGENTS.md:
memory is the only default source for project questions (code on explicit
request), a miss is reported honestly ([[lessons]]), and every entry
carries its date (answers quote it). The vault stays `memory/`: `.glance/`
is a regenerable gitignored cache (truth can't live there), and a
`glance_memory/` rename buys branding but breaks --seed's convention.

## 2026-07-11 — Doctor's contract set by dogfood: measure meaning, gate on clean

An adversarial-vault pass plus executing seed's fill plan settled doctor's
semantics. A marker needs `:`/`(` and sits outside code fences (prose,
quotes and samples don't flag — the third self-flag incarnation in two
days). Dangling covers `#anchor` targets (stripped in vault_stem — also
fixes the graph edge, backlinks and TUI follow), attachments by file
existence, and dead relative `.md` Markdown links; URLs aren't ours. Degree
counts distinct notes; unreadable notes get their own flag and no
judgement. `summary.clean` + exit 0/2/1 turn doctor into a CI gate and
seed's done-check. `--edit` gained `before` (insert above a heading):
history's backfill was otherwise impossible surgically; decisions backfill
appends under the seeded entry — both in chronological insertion order.

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
notes.

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
frozen TCP stress test is unexecuted. (2) The replicated Gate 3 win (15–26pp fewer in-context
contradictions at 0.62–0.76× tokens; 3 runs, 2 domains, p<0.02) beats a
*stale flat pile* — a baseline glance never was: we read the current file,
and "distill, don't append" is already a hand-operated claim store; the
spec's own hard-skeptic baseline (flat RAG + query-time NLI) never ran.
(3) The pipeline needs an extraction LLM + a PyTorch NLI oracle — against
the lightweight ethos. Salvage: Gate 3 as evidence *for* current-state
reads; conflict-flagging as a future `--conflicts` lint; the
pre-registration method as our benchmark template. If pursued: hot-node
test first, then the bolt-on baseline.

## 2026-07-01 — Enhanced keyboard is opt-in; legacy stays the default

`keyboard = enhanced` keeps the kitty protocol so Option/Cmd chords carry
real modifier bits. Legacy stays default: the protocol once leaked onto the
shell at exit (mechanics + un-wedge: [[lessons]]); teardown now clears the
whole stack in both modes. Flip only after field testing on real terminals.

## 2026-07-01 — Docs reorganized: one source per fact + this memory vault

Six root-level prose docs repeated the module map and status and measurably
drifted. Now: README = product, AGENTS.md = the one working guide (CLAUDE.md
just imports it), memory/ = living state, docs/ = DESIGN + MCP + specs +
archive; STATUS.md, context.md and AGENT_FEATURES.md deleted. (Its heads-up
on feat/semantic-minilm was settled by the 2026-07-21 rebase.)

## 2026-07-01 — spike/ and third_party/ are local-only on main

~620 MB of llama.cpp checkouts + gguf models, gitignored so `git status`
stays honest and `git clean -fd` can't destroy them. feat/semantic-minilm
tracks `third_party/llama.cpp` as a submodule; its rebase already drops the
`/third_party/` ignore. (`feature/claim-store` commits its own `spike/`.)

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

On-device spike (`spike/minilm`, local-only): ~150–180 ms model load,
~24 ms/chunk on CPU, no throttle → ship **fp16** + a persistent `.glance/`
cache, ~5 threads. On feat/semantic-minilm (embedder revised: 2026-06-25).

## 2026-06-19 — WYSIWYG Live mode parked after trial

Luca tried slice 1 (`feat/wysiwyg-inline`) and wasn't convinced: the raw
active line's height differs from its rendered height, shifting everything
below ("breathing"). Parked, no PR — don't merge or resume unless he asks.

## 2026-06-19 — No in-TUI AI edit overlay

Built (feat/ai-edit), disliked, fully reverted. glance's AI story is the
*agent layer* (CLI + MCP), not chat inside the editor. Do not rebuild it.

## 2026-06-17 — The pivot: the agent-native memory layer is the product

A terminal reader alone doesn't differentiate; the durable edge is serving
human and agent over the same vault — bounded reads, budgeted retrieval
with a receipt, surgical writes, MCP (docs/DESIGN.md; shipped as M1–M4,
PRs #9–#14, the MCP tools reusing the exact CLI exports).

## 2026-06-16 — PR workflow on a protected main; sole-author commits

Ruleset `protect-main` blocks direct pushes; every change lands by PR.
Commits credit Luca only — history was once rewritten (filter-branch) to
strip assistant trailers; never add them. (The 2026-06-15 C-rewrite entry
was distilled out 2026-07-22: the why lives on as AGENTS.md's "one model,
many sinks" invariant, the arc in [[history]].)

