# Amended Pre-Registration — Confirmatory Run on Non-Overlapping Corpus

**Written:** 2026-06-29  
**Amends:** `spike/artifacts/spike0_frozen_result_20260627/confirmatory_run_preregistration.md`  
**Status: PRE-REGISTERED before any new corpus is fetched or any new run executes.**  
**This file is read-only and must not be modified after commit.**

---

## Amendment Reason

The original pre-registration committed to ~200 articles from Category:Web browsers.
Category:Web browsers is exhausted at 76 qualifying articles (99 total members, 23
disqualified by corpus criteria). There is no non-overlapping article set available
within that category. This amendment specifies the confirmatory corpus and updates the
Gate 1 instrument. All Gate 2 and Gate 3 thresholds are unchanged.

---

## Confirmatory Corpus (pre-registered, fetch not yet started)

**Source categories:**
1. `Category:Internet protocols` — 248 members; comparable factual structure (RFC
   numbers, version dates, deprecation statuses, implementation timelines)
2. `Category:Web standards` — 25 members; comparable factual structure (W3C
   recommendation dates, browser support statuses, deprecation notices)

**Exclusion rule:** Any article already in the 76-article Category:Web browsers corpus
is excluded by title, even if it also appears in the new categories.

**Qualification criteria:** identical to the original — two real revision snapshots,
old revision ≤ 2021-01-01, both snapshots ≥ 800 chars, old ≠ current revid.

**Target N:** as many qualifying articles as the two categories yield under the
above criteria, up to 200. Whatever that N is, it is fixed at fetch time and is not
adjustable after fetching begins.

**Domain note:** Expanding from web-browser articles to internet-protocol and
web-standard articles tests whether the consistency advantage generalizes beyond the
original category. A confirmed PASS across two independent technical domains is a
stronger finding than a single-category result.

---

## Stopping Rule (unchanged and extended)

- One confirmatory run at the achieved N from the categories above.
- Whatever the run produces is the verdict — no further extension.
- If the confirmed N is also < 200 due to qualification failures, that is the final N.
- We will NOT extend to a third category if this run also lands at p=0.09.
- If this run gives Gate 3 PASS (≥20pp, p<0.05), the thesis gate is cleared.
- If this run gives Gate 3 KILL or UNREADABLE, the thesis needs revision.

---

## Gate 1 — Revised Instrument: Direct Degree-vs-Reauth Curve

**The power-law fit is retired.** Alpha was 0.58 at N=44 and 1.30 at N=76, crossing
the entire decision range between runs with a widening CI. A fit whose output is not
converging as N increases is not a usable measurement instrument. It is replaced by
a direct measurement.

### New Gate 1 Instrument

For the top-10 highest-degree scope groups (by number of active claims k):
1. Count oracle-detected contradictions within each group (reauth count): the number
   of claim pairs within the group flagged as contradictory by the frozen oracle.
2. Compute reauth_per_edit = reauth / (k − 1) for each group.
3. Report the full table: rank, scope, k, reauth, reauth_per_edit.
4. Report the distribution: min, median, p75, p95, max of reauth_per_edit.
5. Report the trend: OLS of reauth_per_edit ~ k across the 10 groups (slope, p-value).
   A positive and significant slope means re-authoring cost grows with degree — a
   super-linear propagation warning.

### Pre-Registered Thresholds (set before any new run)

| Condition | Verdict |
|-----------|---------|
| max_reauth_per_edit ≤ 10 AND slope not significantly positive (p ≥ 0.10 or slope ≤ 0) | **PASS** |
| max_reauth_per_edit > 30 OR (slope > 0 AND p < 0.05) | **KILL** |
| fewer than 4 distinct k values across the top-10 groups, OR fewer than 6 groups total | **UNREADABLE** |
| between thresholds (max_rpe in 10–30, or slope borderline) | **KILL** (fail-safe) |

**Rationale for thresholds:**
- max_rpe ≤ 10: editing one entity never forces more than 10 semantic re-auths on any
  real node in the corpus. This is manageable for a knowledge store.
- max_rpe > 30: merging one entity forces 30+ re-auths on the worst node. This is
  practically prohibitive for real-scale deployment.
- Super-linear trend (slope > 0, p < 0.05): re-authoring cost grows with degree,
  meaning the problem compounds as the store scales. This is a thesis-level KILL
  even if current raw counts are low.
- UNREADABLE: fewer than 4 distinct k values means we cannot characterize the trend;
  not enough degree variation to determine whether propagation is bounded.

### What "Curve" Means

The table of (k, reauth_per_edit) for the top-10 groups IS the curve. It shows
whether worst-case re-authoring is:
- Flat or declining with k → bounded (sub-linear, store scales safely)
- Rising with k → super-linear (thesis barrier)

The OLS slope quantifies the trend; the individual rows show the worst case.

---

## Gates 2 and 3 (unchanged)

**Gate 2:** ≥ 95% violation-free → PASS; > 10% violated → KILL. No changes.

**Gate 3:** emergent contradiction-rate reduction ≥ 20pp AND McNemar p < 0.05 →
PASS; < 10pp OR p ≥ 0.05 → KILL; scope_f1 < 0.5 → UNREADABLE. No changes.

Scope scoring: fuzzy token-F1 ≥ 0.5 (pre-registered in the original amendment).
Oracle: same frozen artifact (spike/artifacts/oracle_frozen_20260625T180703/).
Do not retrain or retune the oracle.

---

## What Is Fixed by This Amendment

After any run on the confirmatory corpus:
- These thresholds cannot be changed
- The Gate 1 instrument cannot revert to the power-law fit
- The corpus cannot be extended to a third category
- The oracle cannot be retrained

The output of gates.py on the new corpus, against these thresholds, is the verdict.
