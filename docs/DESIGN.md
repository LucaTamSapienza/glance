# glance — Design & Vision: the agent-native memory layer for Markdown

> Status: north-star design doc. Captures *why* glance exists for the agent era,
> what we are building, and in what order. Decisions here are deliberate; open
> questions are flagged explicitly in §11. Last updated: 2026-06-17.

## 0. North Star

glance is the **protocol layer between an AI agent and a Markdown vault**. The
human gets a beautiful terminal reader/editor; the agent gets an API that hands
it *the right slice at the right cost*. Same files on disk, two consumers.

Every feature is judged by one acceptance test:

> **Would an agent, given the choice, pick glance — because it is cheaper *and*
> the output is better than not using it?**

If the answer is no, the feature does not ship. This is not a slogan: it is the
gate. The whole project succeeds only if an agent reaching for context or making
an edit is strictly better off routing through glance.

## 1. The problem: agent ↔ vault today

When an agent works against a folder of Markdown notes (an Obsidian-style "second
brain"), the state of the art is *read the raw files*. That is expensive and
blind:

1. **Whole-file reads waste tokens.** The agent loads a 2,000-word note to use
   one paragraph.
2. **Discovery is blind.** To find the right note it reads ten. Full-text search
   returns lines, not meaning.
3. **Structure is invisible.** To understand how the brain connects, the agent
   must crawl `[[wikilinks]]` one by one. (This is the exact Obsidian pain
   `--graph` already starts to solve.)
4. **Context goes stale.** The agent re-reads everything each session; it has no
   cheap way to ask "what changed?".
5. **Writing is terrifying.** To change a note the agent either rewrites the
   whole file (costly and risky) or avoids writing at all.
6. **No working memory.** Every session re-derives the vault map from scratch.

## 2. Why this is not "just a Skill"

A reasonable objection: *couldn't a Claude Skill (a prompt) do this?* No — and the
difference is the whole thesis.

- A **Skill is a prompt**. It tells the agent *how to behave*, but the agent still
  uses its own `Read`/`Grep`/`Glob` tools, **burning tokens inside its own
  context window**, with no graph and no structure.
- **glance is an engine**. It does retrieval *outside* the model — it indexes,
  ranks, and slices in native C, and returns one compact bundle. The cost of
  *finding and assembling* context is paid in the binary, not in the context
  window. The agent pays only for the final result.
- A Skill has **no persistent state/index**, cannot enforce a **deterministic
  token budget**, and works **only with Claude**. glance works with *any* agent
  — Claude, Cursor, Cline, GPT, the Agent SDK, cron jobs, CI. For open-source
  adoption, that is the entire addressable market, not a slice of it.

It is not glance *vs* a Skill. The best shape is a **thin Skill or MCP server that
routes the agent to glance**: the Skill is the steering wheel, glance is the
engine. The repo already ships a Claude Code plugin; the MCP server (§5, §9)
generalizes it to every agent.

## 3. Why an agent should use it

- **Tokens.** `glance context "X" --budget 4000` returns ~4k tokens of assembled,
  ranked context instead of the ~60–130k tokens of raw reads it would take to
  find the same thing. An order of magnitude cheaper.
- **Better recall.** Graph expansion + heading structure surface the *linked*
  notes a `grep` would miss.
- **Provenance.** Every returned span carries a stable `note#heading` anchor, so
  the agent's answer is grounded and verifiable.
- **Safe writes.** The agent can file knowledge back without fear of corrupting
  the vault (§5, write side).

## 4. Why a person should download it (to give to their agent)

- **Local, private, no cloud.** Your notes never leave your machine.
- **Zero migration.** Point it at your *existing* Obsidian vault — wikilinks,
  frontmatter, tags, daily notes — and it works.
- **Zero config.** The vault is just a folder; no `--init`, no index command.
- **The token receipt.** Every answer reports how much it saved versus a raw read
  — visible value, and a shareable screenshot (§6).
- **Agent-agnostic.** One tool for every assistant they already use.

## 5. Features

### Read side — token-cheap context
Every read view is **bounded**: it takes `--depth N` (and optional scope such as
`--root <note>`) so that, in a large brain, the agent asks for exactly the depth
it needs instead of receiving a huge JSON full of irrelevant nodes. Low default
depth.

- `outline --abstract [--depth N]` — heading tree plus the first sentence of each
  section. Understand a note in ~80 tokens.
- `read_section "note#heading"` — return only that heading subtree.
- `context "query" --budget N` — the optimal bundle under N tokens (see §6).
- `neighbors --depth N` — the link-graph neighbourhood, one synthesized line each.
- `backlinks --context` — for each backlink, the *sentence around* the citation
  (why others reference it), not the whole referencing note.
- `search` — full-text search over the structured `Doc`.
- `--since <ts>` — delta: only what changed, so the agent re-reads diffs, not the
  vault.

### Write side — surgical and safe
The agent declares **intent + location**; glance performs the surgery. It never
hands the agent a blank cheque to "write anywhere."

- `edit append|insert|replace --under "note#heading" --text "…"` — resolve the
  heading boundaries, insert at the correct point, preserve everything else.
- `edit set-frontmatter --file F --key K --value V` — YAML frontmatter as a
  first-class, queryable/updatable surface.
- Wikilink integrity on rename, de-duplication, and **atomic** writes
  (temp file + rename — already implemented in `fs_save.c`).

**Honest note on the write side.** Today an agent like Claude Code already does
*read whole file → string-match Edit at a point*, and it works. glance's edit API
is still strictly better for three reasons: (a) it inserts **without reading the
whole file first** — the same token win as the read side; (b) it is
**structure-addressed** (`under "note#heading"`) instead of a raw string match
the agent must first read the surrounding text to construct; (c) it keeps the
**vault coherent** (links, frontmatter, dedup, section boundaries) — which no
generic Edit tool does — and it works for **any agent**, not only ones with a
good Edit tool. For Claude-Code-with-Edit specifically the marginal win is
smaller than on the read side, which is exactly why the write API is sequenced
last (§9, M4): the read primitives are the bigger immediate gain.

### Cross-cutting
- **Token receipt.** Every output reports `used X tok · raw read ≈ Y tok ·
  saved Z%`, on the CLI and in MCP tool metadata. Marketing built into the tool:
  every interaction advertises its own value and produces a shareable screenshot.
- **MCP server** (`glance mcp`). Exposes the primitives as native tools — the
  distribution channel (§9).
- **Versioned, documented schema.** Stable IDs, deterministic ordering, a small
  spec others can build on. Standards form when third parties implement *your*
  protocol.
- **Obsidian compatibility** is a requirement, not an extra.

## 6. The retrieval engine (the part that is meant to be new)

This is deliberately *not* the usual chunk-and-embed RAG pipeline. It is
**hybrid, structural, budget-constrained retrieval with provenance**, over a
personal Markdown corpus, fully local.

- **Signal fusion.** Lexical (**BM25**, default) + **structural** (the heading
  tree) + **relational** (the link graph as a relevance prior — 1-hop expansion /
  personalized-PageRank-style propagation) + **dense** (optional, behind a flag).
- **Knapsack-under-budget assembly.** Retrieval is not top-*k*. It is an
  optimization of *information per token under a ceiling*: return the maximum
  value that fits in N tokens.
- **Elastic, coarse-to-fine projection.** The same note can be returned as
  outline / section / abstract / full text. The retriever picks the granularity
  that fits the budget. This is possible *only* because glance owns its renderer
  (md4c → `Doc` → projection); a glamour black box cannot do it.
- **Provenance-first.** Every returned span is citable (`note#heading`), so the
  agent's output is verifiable.

### Budget is a soft target, never a silent cut
A hard top-*k* cut at the token ceiling risks dropping the one span the agent
needed (the "it was at token 4001" problem). The budget therefore:

- returns what fits **plus a truncation manifest** — a cheap list of what was left
  out (anchors + relevance scores) — so the agent *knows* there is more and can
  follow up (`read_section` on a dropped item) or raise the budget. **Never a
  silent drop.**
- falls back **coarse-to-fine**: a highly relevant note that does not fit at
  section granularity is included as its **abstract/outline** (cheap) rather than
  dropped — never invisible, only lower-resolution.
- selects **diversity-aware**, not pure top-score: cover several relevant
  notes/sections instead of over-spending on one, lowering the chance the needed
  span is starved.
- is **optional**: the default is ranked context + receipt with no hard cap; the
  budget is opt-in for cost control, and is designed for an **iterative loop**
  (get the manifest → drill into the one that matters) — cheaper than reading
  everything and safer than a cutoff.

### Semantic search
Behind a flag (`--semantic`); **lexical is the default** so the base user installs
nothing. The dense pipeline ships now — an `Embedder` interface (`embed.c`),
cosine similarity, and lexical+dense fusion in `agent_context` — with a
dependency-free feature-hashing default embedder as the seam. A real encoder
plugs in behind `Embedder` with no change to the retrieval code. Word2Vec is too
weak (context-free word vectors). **Decided: lean MiniLM-class** (e.g. `all-MiniLM-L6-v2`, 22M params, 384-dim) for quality — a
single query embedding is a ~5–15 ms CPU forward pass (negligible heat), and the
one-time index build is incremental thereafter via `fswatch`. Static-distilled
embeddings (model2vec/potion-style) remain the ultra-light fallback for
constrained machines. The index lives in **`.glance/`** inside the vault
(git-ignored, local cache). **Still open (§11): the embedder runtime — to be
locked after an on-device latency/heat benchmark.**

## 7. How it maps onto the existing code (reuse, not green-field)

| Concern                     | Existing module        | Role in the agent layer                     |
|-----------------------------|------------------------|---------------------------------------------|
| Agent-facing JSON exports   | `agent.c`              | extend with the new bounded views           |
| Vault scan + wikilinks      | `vault.c`              | discovery, resolution                       |
| Link graph                  | `graph.c`              | neighbours, graph prior for retrieval       |
| Structured `Doc` + renderer | `render.c`             | coarse-to-fine projection (outline/section) |
| Full-text search            | `search.c`             | lexical/BM25 base                           |
| Atomic write                | `fs_save.c`            | the write API                               |
| Parent-dir watch            | `fswatch.c`            | index invalidation                          |
| **New**                     | `index/`, `mcp`, `edit`, `receipt` | BM25 (+optional dense), MCP server, surgical edits, token accounting |

## 8. The moat

- **Local graph + structure with no mandatory embeddings** — privacy, zero
  per-query cost, deterministic, reproducible.
- **Stable citations** (`note#heading`) — grounded, verifiable output.
- **Owned renderer** — multi-granularity projection a glamour-based tool cannot do.
- **Safe writes** — the retention feature: an agent that can *maintain* the brain.

## 9. Milestones (sequenced for adoption)

- **M1 — bounded reads + budgeted retrieval + token receipt. ✅ shipped.** See
  the CLI surface below.
- **M2 — `glance mcp`. ✅ shipped.** A stdio JSON-RPC 2.0 MCP server exposing the
  eight reads as native tools (`vault_context`, `vault_section`, `vault_outline`,
  `vault_neighbors`, `vault_backlinks`, `vault_since`, `vault_links`,
  `vault_graph`); the tool bodies are the same `agent.c` exports, captured and
  framed. Native in Claude Desktop / Cursor / the SDK — the distribution channel.
  Wiring + tool reference: `docs/MCP.md`. New modules: `json.c` (a small JSON
  parser) and `mcp.c` (the server). Unit-tested (`json_test`, `mcp_test`).
- **M3 — semantic search behind a flag. ◑ infrastructure shipped; model pending
  the benchmark.** The full dense pipeline is in place: an `Embedder` interface
  (`embed.c`), cosine similarity, and `--context --semantic` fusing the
  embedding score with the lexical BM25 score (notes a keyword search misses can
  surface). The shipped default embedder is dependency-free (feature-hashed token
  n-grams) — a *structural* signal that exercises the whole pipeline, **not** a
  semantic model. The real quality jump is a MiniLM-class encoder plugged in
  behind the same `Embedder` interface, plus an embedding cache in `.glance/`;
  that step is deliberately deferred to the on-device latency/heat benchmark we
  agreed to run together (§11). Lexical stays the default.
- **M4 — surgical write API.** Closes the loop; the moat.

### M1 — shipped command surface
All print JSON to stdout; every read view is bounded so it stays token-cheap.

| Command | What it returns |
|---|---|
| `glance --outline FILE [--depth N] [--abstract]` | heading tree, depth-bounded, each heading's first paragraph as `abstract` |
| `glance --section "FILE#Heading"` | one heading's subtree + a token `receipt` (anchor matches the heading text or its slug) |
| `glance --context "QUERY" DIR [--budget N]` | the budgeted bundle: `{query,budget_tokens,chunks,truncated,receipt}` |
| `glance --neighbors "Note" DIR [--depth H]` | link-graph neighbourhood to H hops, with `direction` (outbound/backlink/both/path) |
| `glance --backlinks "Note" DIR [--context]` | notes linking to Note; `--context` adds the citing line |
| `glance --since TS DIR` | notes modified after Unix time TS |

Modules (all pure, unit-tested under ASan/UBSan): `section.c` (anchor →
subtree + abstract), `receipt.c` (token estimate + saved-% receipt), `bm25.c`
(Okapi BM25 lexical core), `context.c` (the budget planner: score order,
diversity, coarse-to-fine, truncation manifest). The vault I/O, BM25 indexing,
graph prior, and JSON emission live in `agent.c`.

The read side + MCP + receipt is the adoption wedge (maximum perceived value,
low risk, mostly refinement of existing code). Write comes last because, for the
agents that already have a decent Edit tool, the read side is the bigger
immediate win.

## 10. The growth loop (why OSS adoption compounds)

1. Someone sees the **token-receipt screenshot** — *"I gave Claude my Obsidian
   vault; it answered in 4k tokens what would have cost 130k. Here's the receipt."*
2. `brew install glance` → `glance mcp` → paste three lines into the Claude
   Desktop / Cursor config. Two minutes, zero config.
3. They query their vault → see *their own* receipt → share it → back to step 1.

Everything we build either serves this loop or waits.

## 11. Open questions (decide later, together)

- ~~**Semantic model.**~~ **Decided: MiniLM-class** (quality), with
  model2vec/potion-style static embeddings as the ultra-light fallback.
- ~~**Index location.**~~ **Decided: `.glance/` inside the vault** (git-ignored
  local cache, invalidated by `fswatch`).
- **Embedder runtime in C.** The `Embedder` interface (`embed.c`) is in place and
  a real encoder drops in behind it with no change to retrieval. Still to lock,
  after an on-device latency/heat benchmark of MiniLM on Apple Silicon:
  onnxruntime vs llama.cpp/ggml embeddings vs an optional sidecar installed only
  for the semantic power-up — plus the embedding cache layout under `.glance/`.
- **Name.** Stays `glance` for now; discoverability handled via README / a
  Homebrew tap (note the OpenStack `glance` CLI name collision).

## 12. Success metrics

- **% tokens saved** (the receipt) versus raw reads — the headline number.
- **Answer quality / recall** versus reading raw files for the same question.
- **Adoption** — installs, GitHub stars, presence in MCP directories.
