# Handoff — agent-native memory layer (M1–M4), for a review & level-up pass

> **Archived 2026-07-01.** Historical: the stacked PRs #9–#12 described below
> merged long ago, the suite count and "M3 is infrastructure-only" are
> outdated, and the review it asked for happened (see `REVIEW.md` beside this
> file). Kept as the record of how M1–M4 were built. Current state:
> [`memory/status.md`](../../memory/status.md).

You are picking up the **agent-native memory layer**: glance as the protocol
layer between an AI agent and a Markdown vault. Four milestones (M1–M4) were
built in one session as **stacked branches/PRs**. This file is the single entry
point to review all of it and take it further. Read it top to bottom, then dig.

## 0. Orient yourself (read order)

1. `CLAUDE.md` — project conventions (non-negotiable: per-function `/* */`
   comments in English, a unit test per behaviour, `make test` green under
   ASan/UBSan, **sole-author commits — no Co-Authored-By**, vi-style keys, no
   code-fence autopairing).
2. `docs/DESIGN.md` — the north-star spec: why glance (not a Skill), the
   retrieval engine, the milestones, and the **open questions (§11)**.
3. `docs/MCP.md` — how the MCP server is wired and its tool reference.
4. This file — state, how to build/verify, and **where to push next**.

## 1. What exists (branches & PRs — stacked, merge in order)

| PR | Branch | Milestone |
|----|--------|-----------|
| #9  | `docs/design-agent-memory` (base `main`) | **M1** bounded reads + budgeted retrieval + the design doc |
| #10 | `feat/m2-mcp-server` (base M1) | **M2** `glance mcp` — stdio JSON-RPC MCP server |
| #11 | `feat/m3-semantic` (base M2) | **M3** semantic fusion (INFRASTRUCTURE only — see §4) |
| #12 | `feat/m4-write-api` (base M3) | **M4** surgical write API |

Each PR's base is the previous branch, so each diff shows only its milestone;
merging in order auto-retargets the next onto `main`. **The tip
`feat/m4-write-api` contains all of M1–M4** — review there. Direct pushes to
`main` are blocked by a ruleset (`protect-main`, PR required).

## 2. The surface that shipped

CLI (all print JSON; every read is bounded/token-cheap; `--context`/`--section`
carry a token receipt):

```
glance --outline FILE [--depth N] [--abstract]
glance --section "FILE#Heading"
glance --context "QUERY" DIR [--budget N] [--semantic]
glance --neighbors "Note" DIR [--depth H]
glance --backlinks "Note" DIR [--context]
glance --since TS DIR
glance --edit FILE append|insert|replace "Heading" "text"
glance --set-frontmatter FILE KEY VALUE
glance mcp     # MCP server: 10 tools (8 read + vault_edit, vault_set_frontmatter)
```

New modules (all pure + unit-tested unless noted): `section.c`, `receipt.c`,
`bm25.c`, `context.c` (M1); `json.c`, `mcp.c` (M2); `embed.c` (M3); `edit.c`
(M4). The vault I/O + JSON emission live in `agent.c`.

## 3. Build & verify (IMPORTANT gotcha)

```sh
make            # build ./glance and ./glance-render
make test       # 23 suites under ASan/UBSan — must stay green
```

**Never run `./glance …` as a background command or with `&`** in this sandbox:
backgrounded glance spins at 100% CPU (job-control / no controlling tty). Run
agent CLIs **synchronously**, with a tool-level timeout. Every command finishes
in <0.2s. (A previous session orphaned spinning processes this way.)

## 4. Honest state & known soft spots (your review targets)

The happy paths are tested and clean; here is where I would point an adversarial
pass. None of these are blockers — they are the "next level".

- **M3 is infrastructure, not semantics (the headline gap).** `embed.c`'s
  default embedder is feature-hashed n-grams — a *structural* signal
  (≈ weighted token overlap), **not** a semantic model. The real jump is a
  MiniLM-class encoder behind the `Embedder` interface, **gated on an on-device
  latency/heat benchmark to run WITH the user** (he's an NLP engineer; decided:
  MiniLM-class, index in `.glance/`). Fusion weight `CTX_SEMANTIC_LAMBDA` is
  hardcoded; embeddings are recomputed every query (no cache) — fine for hashing,
  too slow for MiniLM ⇒ needs the `.glance/` embedding cache.
- **Graph prior only re-ranks; it can't introduce notes.** In `agent_context`
  the 1-hop prior boosts sections whose note is a neighbour of a note that
  *already* has BM25 > 0. It will not surface a linked note with zero lexical
  match — so "find the linked note grep missed" is only partly delivered. A v2
  graph-EXPANSION would add discounted neighbour sections as fresh candidates.
- **Token receipt is a heuristic** (`max(bytes/4, words)`), not a real
  tokenizer; the saved-% is directional, not exact. Consider a real BPE or a
  calibration.
- **No persistent index.** Every `--context` rebuilds the BM25 index over the
  whole vault. For large vaults this is repeated work; an incremental cache in
  `.glance/` (invalidated by mtime / the existing `fswatch`) would pay off, and
  it shares the cache story with the embeddings.
- **Budget planner is greedy** (two-pass diversity + coarse-to-fine), not an
  optimal knapsack. Good enough; note it if you formalize.
- **`edit.c` is ATX-only.** Setext headings (`===` / `---` underlines) are not
  matched as edit targets; the frontmatter parser is line-based and simple
  (top-level `key: value`, no nested YAML / quoted-colon values). Fenced-code
  headings ARE correctly ignored (tested).
- **`section_text` walks each line twice** (size then copy) — minor.
- **MCP capture via `dup2`/`tmpfile` per call.** Works and is tested, but a
  cleaner design is to parameterize the `agent.c` exports on a `FILE*` (or a
  string sink) instead of redirecting stdout. Also: only `tools` capability is
  advertised — no MCP `resources`/`prompts`.

## 5. Candidate "next level" directions (pick with the user)

Highest-leverage first, roughly:

1. **Semantic for real** — wire a MiniLM-class encoder behind `Embedder` +
   `.glance/` embedding cache, after the benchmark. Unlocks M3's actual value.
2. **Persistent/incremental index** (BM25 + embeddings) in `.glance/`, invalidated
   by mtime/`fswatch`. Makes `--context` cheap on big vaults; shared with #1.
3. **Graph-expansion retrieval** — let the prior introduce linked zero-BM25
   notes as candidates (the real "Obsidian graph beats grep" story).
4. **`glance ask "…"`** — a one-shot that assembles a context bundle and calls an
   LLM, printing the answer + the token receipt. This is the 30-second viral
   demo / the OSS growth loop (see DESIGN.md §10).
5. **README-as-product** — the repo's front page is the OSS funnel: the
   token-receipt screenshot, the 3-line MCP config, one-line positioning. Also
   decide the `glance` name collision (OpenStack client) — Homebrew tap?
6. **Real tokenizer** for accurate receipts; **setext + robust frontmatter** in
   `edit.c`; **MCP resources/prompts**.

## 6. Acceptance test (the bar for everything)

From DESIGN.md: *would an agent, given the choice, pick glance because it is
cheaper AND the output is better than not using it?* Every change is judged by
that. Keep `make test` green and ASan/UBSan-clean; ship a test with each change;
update `DESIGN.md` / `STATUS.md` / `context.md` in the same change.
