# Plan: A consistency model for mutable LLM knowledge stores (glance as one instantiation)

## The single claim everything orbits

> A content-addressed, provenance-bearing **claim store** gives an LLM **persistent,
> cross-query-consistent, conflict-flagged single-source-of-truth** under a stream of edits:
> resolutions stay resolved and identical across queries, where flat-corpus RAG — *even with
> a query-time contradiction detector bolted on* — re-discovers, re-pays, and reconciles
> differently each time — within a measured token bound (≤ X% over flat top-k). Storing claims
> **absolutely** makes torn-read freedom hold by construction and dissolves the
> compression-inherited currency–cost–latency trilemma.

Everything below either proves this claim or is explicitly parked. The contribution is the
**principle + the consistency model**, not the system. glance is one instantiation.

## What this is (and what changed)

The thesis is settled and not up for relitigation: not a token compressor (token prices
deflate, prompt caching reuses repeats free, windows are exploding) but a **consistency
engine** — knowledge as normalized, mutable, single-source-of-truth state read as a coherent
canonical view. The bar is now *PhD-level importance*: a portable principle and a named
consistency model that change how the field thinks about LLM knowledge stores — not a tool
that works. So the principle is lifted off glance, the consistency model is a first-class
theory object, and the spike is re-derived to kill the *consistency* thesis, not the dead
compression one.

## 1. The principle, stated independently of glance

A **claim store** maintains knowledge as a versioned set of **claims**, not documents:

- **Claim** `c = ⟨body, scope, validity, provenance⟩`, stored **absolutely** (self-contained;
  no claim's body depends on another claim's content), content-addressed by `h(c)`.
  - *scope* = the entities/context the claim is about; *validity* = conditions/time it holds;
    *provenance* = source span(s), author (model + merge-policy version), timestamp.
- **Operations:** `assert`, `supersede` (edit → new version; old retained), `retract`, over a
  graph of **reference edges** that serve retrieval / scope-linking / navigation — *not*
  reconstruction pointers — with **referential integrity**. Because claims are absolute,
  editing one cannot semantically corrupt its neighbors.
- **View composition:** for an information need, assemble a set of currently-valid claims
  into a coherent context that **faithfully entails exactly its source claims**.
- **Consistency model:** the guarantee about what a reader observes relative to edit history
  (§2).

**glance as one instantiation** (answering "does this only work because of markdown
wikilinks?"): a vault is a claim store, a note is a claim container, `[[wikilink]]` is a
*pre-existing* claim-reference edge (a convenience, not a dependency), `.glance/` is the
compiled claim index. The same abstraction is realizable over records, a codebase, or a chat
log — wikilinks just hand glance the reference graph for free. The plan keeps a clean
"abstraction ⟂ realization" seam so the claim generalizes.

## 2. The consistency model (the theory contribution)

Define what *consistent* means for an LLM knowledge store. Four properties:

1. **Torn-read freedom** (safety): holds *by construction* — claims are absolute, so a view
   never mixes a claim with content authored against a superseded version. (Under the dead
   compression thesis this needed active defending; absolute storage makes it free.)
2. **Conflict-flagging** (safety, the **headline guarantee**): two currently-valid claims
   with overlapping scope and contradictory bodies are *detected and surfaced with both
   provenances*, never silently presented. **Prevention** (auto-resolution so the model never
   sees a conflict) is a *stronger, demoted, opt-in* mode under eager resolution — not the
   headline.
3. **Read-your-edits** (liveness): a reader's own prior `assert`/`supersede` is reflected in
   its next read.
4. **Bounded staleness** (currency): each claim in a view is current as of the snapshot, or
   explicitly version-pinned/flagged stale.

**The trilemma, bounded (the honest theory object).** The currency–cost–latency tension —
O(1) writes / always-current reads / bounded latency, pick two — *only* arises when dependents
are **semantically authored against a specific claim version** (residual coding, inherited from
the dead compression thesis). **Storing claims absolutely dissolves it**, at the cost of
cross-claim compression — which is the *correct* trade once tokens are cheap. The sharper
result is that relationship, not "here is a trilemma." The tension survives only in a weaker
form for **eager merge / canonicalization propagation** (when a merge must propagate to
already-derived claims); the spike measures whether *that* propagation amplifies.

## 3. Two promoted contributions

- **Scoped-claim canonicalizability.** Real knowledge is scoped ("the deadline is Friday" —
  *which project?*; "X is deprecated" — *as of which version?*). Stripping a claim from its
  scope lets the canonicalizer silently merge two textually-identical claims that are true in
  different contexts — the worst silent-corruption vector. So canonicalization is
  **scope-aware**: identity requires matching scope/validity, not just body. *What makes a
  claim canonicalizable without destroying its truth-conditions* is a named research question,
  measured by canonicalization **precision/recall** with scope-confusion as a tracked error
  class.
- **Schema stability across model generations** (named open problem, not claimed solved).
  Deterministic replay reproduces *one frozen* canonicalization — that is **build
  reproducibility**, not canonicalization stability. Re-canonicalizing next year with a new
  model yields a different schema. Mitigations only gestured at: confidence-thresholded /
  human-locked decisions, pinned model versions, schema-drift detection between versions.

## 4. The baselines that must be beaten

Two, both *run* not assumed away:

- **Easy skeptic — long-context reconciliation:** a frontier model handed the entire raw,
  drifting corpus to reconcile itself. Fails because it can't reconcile what overflows the
  window (and degrades with distractors), and reconciliation is non-persistent / non-auditable.
- **Hard skeptic — the bolt-on (the real competitor):** flat RAG **+ a query-time NLI
  contradiction detector + an entity/scope linker.** This retrofits *flagging* at query time,
  so flagging alone is **not** a moat. The store's unique, non-retrofittable value is exactly
  two things: **(1) cross-query single-source-of-truth** — every query gets the *same* reconciled
  answer (the bolt-on can reconcile differently each time); and **(2) persistence of resolution**
  — once a conflict is canonicalized it stays resolved (the bolt-on re-discovers and re-pays it
  every query). **Foreground these two, not flagging.** If we can't beat the bolt-on, the store
  is ceremony — and we learn it in week one.

## 5. Spike 0 — three kill gates re-derived from the consistency thesis

Smallest possible end-to-end on a **mutating** corpus. Define the scoped-claim tuple; crude
scope-aware canonicalization; content-addressed claims + references. Each gate is designed so
a **negative result is itself a contribution** ("we attempted a normalized mutable LLM store;
here is the fundamental barrier").

1. **Merge-propagation amplification under a hot-node edit.** Default to **absolute claims**
   (reach for residual coding only if the spike surfaces a concrete need). With absolute storage
   a claim edit is a cheap local write, so this gate targets the *real* question: when a
   **canonicalization/merge** decision changes (a high-degree entity is split or merged), how far
   does it propagate, and how much is cheap relinking vs. semantic re-authoring? Measure the
   propagation-cost *distribution*. **Kill** if merges routinely amplify super-linearly with no
   cheap containment. *Negative-result deliverable:* the bounded trilemma, quantified for eager
   propagation.
2. **View faithfulness.** Composition re-runs an LLM per query with no provenance and can
   drop/fuse/hallucinate-bridge claims — reintroducing corruption at the only layer the model
   sees. **Test:** does the composed view provably entail *exactly* its source claims, no more,
   no fewer? **Kill** if composition routinely distorts canon (⇒ integrity never reaches the
   model; the claim layer is ceremony). *Negative-result deliverable:* a taxonomy of view-
   composition faithfulness failures.
3. **Consistency advantage, independently measured.** Same edit stream → (a) the store vs.
   (b) a flat pile; for matched queries, how often does the assembled context contain a
   contradiction? **Constraint:** the contradiction oracle must be *methodologically
   independent* of our canonicalizer (different technique + human-validated subset) — else we
   grade the system with a copy of itself. Cover **emergent** contradictions, not just
   injected. **Kill** if the gap is insignificant or vanishes under the independent oracle
   (⇒ flat RAG is already conflict-robust — surprising and publishable). **Measure scope-
   extraction quality as an explicit input here** — unreliable scope extraction poisons conflict
   detection with scope-confusion errors and makes this gate unreadable; track it, don't assume
   it. *This gap is the entire pitch.*

Token-vs-dedup stays only as a **cost sanity check** ("within a reasonable factor?"), never a
gate.

## 6. Cut list (explicitly parked — future work, off the critical path)

Multi-resolution cascades (L0–L4); the compression-vs-recoverability Pareto; the rate-
distortion / foveated read path; learned/KV tiers; HotpotQA/MuSiQue/2Wiki as anything beyond
a one-line sanity footnote (they are mutation-free and cannot exercise consistency). View
composition is reduced to **single-fidelity faithful** composition — faithfulness matters,
multi-resolution does not.

## 7. If the spike survives — the minimal module set

Lean, reusing existing primitives (each `src/x.c`+`.h`+`tests/x_test.c`, in `CORE` and the
`test:` target, ASan/UBSan-clean):

- **`claim.c`** — scoped-claim extraction from a `Doc` (reuse `toc_build` `src/toc.c:19`,
  `line_text` `render.h:79`); content-hash + provenance. **Scope extraction is load-bearing**
  (its noise propagates into canonicalization and conflict detection), so its quality is a
  *measured input* to Gate 3, not an assumption.
- **`canon.c`** — scope-aware canonicalization: C candidate generation (shingling/Jaccard;
  link candidates via `vault_stem`/`graph_find`) → `--normalize-plan` JSON (`json_str`
  `agent.c:16`); ingest LLM-confirmed `decisions.json` as **provenance-bearing, reversible**
  merges. The research core; P/R measured.
- **`store.c`** — content-addressed, versioned claim store under `.glance/` (`vault_root`
  `vault.h:37`): assert/supersede/retract, reference edges, snapshot reads, referential
  integrity, conflict-flagging, provenance log.
- **`view.c`** — single-fidelity **faithful** view composition (entailment-checked).
- **CLI** (`main.c:112`, `run_export` `:68`): `--normalize-plan`, `--normalize-apply`
  (deterministic replay), integrity/conflict/provenance queries. Claude Code plugin skill
  runs the LLM-confirm step; `make test` mocks it with committed fixtures (offline).

## 8. Evaluation

The **mutating workload is the eval** (agent long-horizon memory / daily-edited wiki /
evolving codebase): thousands of edits; does retrieved context stay torn-read-free and
conflict-flagged while flat RAG drifts and self-contradicts, at comparable token cost, with
provenance? Metrics, all re-derived from the claim: contradiction rate in assembled context
(independent oracle, emergent + injected) · canonicalization precision/recall incl. scope
confusion · view-faithfulness (entailment) rate · the currency–cost–latency operating point ·
token cost held **within a defined bound (≤ X% over flat top-k), measured not assumed**.
Baselines run head-on: **the bolt-on (flat RAG + NLI detector + scope linker)** and
**long-context reconciliation** (§4), plus flat top-k, query-time-dedup top-k, prompt caching,
GraphRAG, RAPTOR. QA sets = sanity footnote only.

## 9. Verification & housekeeping

- Spike gates evaluated explicitly before the §7 build starts; negative-result deliverables
  written up regardless of outcome.
- `make test` green, ASan/UBSan-clean, new `tests/{claim,canon,store,view}_test.c`.
- **Deterministic replay**: apply a frozen `decisions.json` twice → byte-identical store.
- **Provenance/reversibility**: undo a merge → prior state; every claim resolves to its
  source spans. **Integrity**: dangling-reference detection after retract; bad-merge
  detection/repair. **Conflict-flagging**: an injected scope-matched contradiction is
  surfaced, not silently merged.
- Status docs updated same-change: `context.md`, `STATUS.md`, the auto-memory plan (CLAUDE.md).

## Step 0 (once approved)

Switch to `master`, pull, branch `feature/claim-store`, re-verify the `file:line` references
against `master` (exploration was on `feature/legend`; core substrate is stable across both).
