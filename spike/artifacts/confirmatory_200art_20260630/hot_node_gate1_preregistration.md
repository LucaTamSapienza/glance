# Hot-Node Gate 1 Pre-Registration

**Written:** 2026-06-30
**Status: PRE-REGISTERED — no hot-node run has been executed.**
**This file is read-only and must not be modified after commit.**

---

## Why This Is Needed

Gate 1 has been UNREADABLE in three consecutive runs. The cause in all cases is that
the corpus+extractor combination produces max active k=14. The gate exists to answer:
"does a hot node explode under merges/edits?" We have never had a hot node to test.

This pre-registration specifies a deliberate stress test: construct a high-degree entity
by design, force a merge/split event on it, and measure worst-case re-authoring cost
directly. The question is answered once.

---

## What Counts as a Hot Node

A scope group with k ≥ 25 active claims — meaning the scope label appears as the primary
scope for 25 or more currently-valid claims across the store. This is the stress regime
the original Gate 1 was designed to probe and never reached.

---

## Construction Method (pre-registered)

Select or construct one hot-node entity using one of the following methods (in preference order):

1. **Cross-article scope merging (preferred):** From the existing 200-article corpus,
   identify all scope groups within token-F1 ≥ 0.5 of a common entity (e.g., all scopes
   that token-match "IP multicast", "internet protocol", or similar high-frequency terms).
   Merge them into a single canonical scope via a committed `merge_decisions.json`.
   Record the resulting k. If k ≥ 25, proceed. If not, move to method 2.

2. **Targeted article expansion:** Fetch 20–30 additional Wikipedia articles on a single
   well-defined protocol (e.g., TCP — appears in dozens of protocol articles). Extract
   claims with the same prompt and model (current extractor: gpt-4o-mini). Merge all
   TCP-scoped claims into one group. Record k. Pre-register the article list before fetching.

3. **Synthetic injection (last resort):** Inject 25 synthetic claims all scoped to one
   entity, document them as synthetic, run the gate on the synthetic hot node only.
   This is the weakest evidence; if used, write it up as a controlled synthetic stress
   test, not a corpus result.

The method used must be committed before any k is observed.

---

## Measurement (unchanged from amended pre-registration)

For the hot-node entity (k ≥ 25):
1. Simulate a merge event: change the canonical scope label, propagate to all k claims.
2. Count oracle-detected contradictions within the merged group (reauth count).
3. Compute reauth_per_edit = reauth / (k − 1).
4. Report: k, reauth, rpe, scope label, method used.

Report also the rpe values for the top-5 other scope groups (k < 25) for comparison.

---

## Pre-Registered Thresholds (set before any hot-node k is observed)

| Condition | Verdict |
|-----------|---------|
| rpe ≤ 5 on the hot node | **PASS** — propagation cost bounded even at high degree |
| rpe > 20 on the hot node | **KILL** — hot-node merges are prohibitively expensive |
| 5 < rpe ≤ 20 | **MARGINAL** — report as-is with discussion; not a thesis KILL but warrants scope in §7 |
| k < 25 achieved after construction attempt | **UNREADABLE** — document as structural limit |

Rationale: at k=25, rpe=5 means 125 total re-auths per merge — manageable for a store
with provenance. rpe=20 means 500 re-auths — practically prohibitive. These thresholds
are set relative to hot-node scale, not the k=12–14 regime already measured.

---

## What This Does Not Change

- The pre-registered NO-GO verdict on the confirmatory run stands.
- The frozen oracle is unchanged.
- Gate 2 and Gate 3 results are unchanged.
- The extractor-comparison run (robustness check) is independent of this gate.

This pre-registration covers Gate 1 only. Its result feeds the §7 build decision
alongside the extractor-comparison run, not independently.

---

## Stopping Rule

One hot-node construction attempt per method, in order. If method 1 produces k ≥ 25,
run the measurement. Do not also run method 2 "for comparison." One run, one verdict.
