# Spike 0 — Work Order (throwaway; product = three numbers + go/no-go)

Tests the consistency thesis from `2026-06-25-llm-claim-store-consistency.md` before any
glance C module is built. Ugly is allowed. If a line of code does not feed a gate
measurement, it is not written. Code lives in `spike/` (Python, gitignored venv/data), is
**not** the §7 module set, and is thrown away after the go/no-go.

## LLM bridge

No `ANTHROPIC_API_KEY` in this environment; the authenticated **Claude Code CLI** is the
bridge: `claude -p --model claude-haiku-4-5-20251001 '<prompt>'` (strip ``` fences from
output). ~13 s/call → corpus is sized to keep wall-clock sane (start ~60 articles, scale if a
gate is borderline). This realizes the "plug in Claude Code as a plugin" path.

## Corpus

English Wikipedia domain subset (~60–120 articles, one coherent category, 1–5k tokens each —
large files, to stress extraction and view composition). **Edit stream = real revision
history** over a fixed window (genuine additions / fact-updates / deletions / emergent
contradictions). Pulled via the MediaWiki API.

> **Caveat to record at the Gate 3 number (Guard, do not act on now):** Wikipedia is
> human-edited *toward* consistency, so its emergent-contradiction base rate may run lower
> than real target domains. If Gate 3 is a near-miss, check the corpus base rate before
> concluding the thesis failed — a consistency-clean corpus is a weak test, not a verdict.

## Build order (nothing outside this list)

`oracle.py` (first, frozen) → `corpus.py` → `extract.py` → `store.py` → `canon.py` →
`read.py` → `gates.py`.

## Guard 1 — Oracle frozen before any canonicalizer output

Build `oracle.py` (independent contradiction detector, **NLI method**, distinct from the
canonicalizer's entity/scope logic). Validate it on a labeled set, **record precision/recall**,
and commit the oracle + its validation as a **timestamped read-only artifact**
(`spike/artifacts/oracle_frozen_<ts>/`) *before* extraction or merge code runs once. **No
post-hoc threshold retuning** after seeing any store result — a disappointing Gate 3 is a
result, not license to retune the judge.

## Gates — pass/fail pre-registered; each is PASS / KILL / **UNREADABLE**

`gates.py` must distinguish UNREADABLE (fix the instrument, re-measure) from KILL (thesis
barrier). UNREADABLE never means abandon.

### Gate 1 — Merge-propagation amplification (Guard 3)
Edit/merge a top-degree entity; count semantic re-authorings vs node degree `k`; fit
`re-auth ∝ k^α`. **Also print raw per-edit re-authoring counts for the 3 highest-degree nodes
by hand** (more trustworthy than the fit). Report the fit's **confidence interval**.
- **PASS:** α ≤ 0.6 and p95 ≤ 15/edit.
- **KILL:** α ≥ 1.0 or p95 > 40/edit.
- **UNREADABLE:** CI spans ~0.6–1.2 (too few high-degree nodes to fit) → trust the raw counts,
  re-measure with more nodes.

### Gate 2 — View faithfulness
200 (query→view) pairs; bidirectional NLI between view and its source claims; violation =
hallucinated/fused claim or materially dropped source.
- **PASS:** ≥ 95% views violation-free. **KILL:** > 10% violate. (5–10% → fix composition.)

### Gate 3 — Consistency advantage (Guards 2 & 4)
Same edit stream → store vs flat pile; contradiction rate in assembled context per the
**frozen independent oracle**, over 200 matched queries. Report scope-extraction precision/
recall alongside.
- **Split injected vs. emergent — two columns, never blended.** The PASS threshold must be met
  on the **emergent** column to count; injected is reported for context only.
- **PASS:** emergent contradiction-rate reduction (flat − store) ≥ 20pp, McNemar p < 0.05,
  surviving restriction to the oracle's validated regime.
- **KILL:** emergent reduction < 10pp, or not significant, or vanishes under the independent
  oracle.
- **UNREADABLE:** scope-extraction F1 < 0.5 — cannot separate "store doesn't help" from "noisy
  scope masked the help"; fix extraction, re-measure (not KILL).

Token sanity (not a gate): store context ≤ 1.5× flat top-k.

## Output

`gates.py` prints the three gate results (each PASS / KILL / UNREADABLE, Gate 3 with both
columns + the corpus base-rate caveat), the token-sanity number, and an overall go/no-go.
That output — not another plan — is the next deliverable.
