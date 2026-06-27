# Confirmatory Run Pre-Registration — 200-Article Corpus

**Written:** 2026-06-27  
**Status:** Pre-registered BEFORE any 200-article run is executed.  
**This file is read-only and must not be modified after commit.**

---

## Stopping Rule

One confirmatory run at N=200 articles. Whatever that run produces is the verdict.
We will NOT extend to N=400 if the 200-article run gives p=0.09 again.

If the 200-article run gives:
- Gate 3 emergent ≥ 20pp AND McNemar p < 0.05 → PASS → §7 build proceeds
- Gate 3 emergent < 20pp OR p ≥ 0.05 → KILL (confirmed) → thesis revision required

A confirmed KILL at ~18pp is a publishable result in itself: "normalized claim
storage reduces contradiction exposure ~18pp at 0.76× token cost; here is the
consistency model." It is not license to re-run at 400 articles.

---

## Corpus

- **Category:** Category:Web browsers (English Wikipedia) — same as Spike 0
- **Target size:** 200 articles (or all available if fewer than 200 qualify)
- **Qualification criteria:** same as Spike 0 (two real revision snapshots, old ≤
  2021-01-01, both snapshots ≥ 800 chars, old ≠ current revid)
- **Extraction:** same `claude -p claude-haiku-4-5-20251001` bridge, 12 claims per
  snapshot, same prompt template

---

## Thresholds (unchanged, no loosening)

| Gate | PASS | KILL | UNREADABLE |
|------|------|------|------------|
| Gate 1 | α ≤ 0.6 AND p95 ≤ 15/edit AND CI width ≤ 0.6 | α ≥ 1.0 OR p95 > 40/edit | CI width > 0.6 |
| Gate 2 | ≥ 95% violation-free | > 10% violated | — |
| Gate 3 (emergent) | reduction ≥ 20pp AND McNemar p < 0.05 | reduction < 10pp OR p ≥ 0.05 | scope_f1 < 0.5 |

The emergent column remains the verdict column. Injected column is reported for
context only and does not affect PASS/KILL.

---

## Scope Scoring Method (pre-registered with justification)

**Method:** Token F1 ≥ 0.5 after stopword removal (fuzzy scope matching), used in
both the scope-F1 measurement and the context contradiction-rate check.

**Justification (written before this run executes, on principled grounds):**

LLMs extract scope strings non-deterministically. Two calls about the same entity
from different text snippets produce varied surface forms: "Internet Explorer" vs
"Internet Explorer browser", "Web browser users" vs "Global browser users", "hoax
study scope" vs "AptiQuant study methodology". These are semantically equivalent
scope assignments, not scope confusion errors. Exact string matching treats them as
failures, giving artificially low scope-F1 that triggers the UNREADABLE rule and
prevents measurement of the consistency advantage — even when the extraction is
functioning correctly.

Token overlap F1 ≥ 0.5 (significant-word intersection / union, with stopwords
removed) is a standard entity normalization technique that handles these surface
variations without collapsing genuinely distinct entities. It does not merge "Firefox"
and "Google Chrome" (no token overlap after stopword removal) or "2018 model" and
"2022 model" (the numeric tokens differ). The threshold of 0.5 requires majority
token overlap, preventing over-merging.

**Temporal disclosure:** This method was first introduced mid-run in Spike 0 in
response to seeing scope_f1=0.104 under exact matching. The justification here is
on principled grounds, not derived from the result it produces. The reader should
note this temporal ordering. The Spike 0 result file documents the exact-match
scope_f1 (0.104) as the original pre-registered value.

**Threshold for UNREADABLE in this run:** scope_f1 (fuzzy) < 0.5

---

## Oracle

Reuse the frozen oracle from Spike 0:
- **File:** `spike/artifacts/oracle_frozen_20260625T180703/FROZEN`
- **Model:** MoritzLaurer/DeBERTa-v3-base-mnli-fever-anli
- **Threshold:** 0.95 (frozen, do not retune)

No retraining, no threshold adjustment, regardless of what the 200-article run shows.

---

## Analysis Plan

- Gate 1: same power-law fit on all scope groups with k ≥ 2; expect tighter CI
  with more high-degree nodes
- Gate 2: same per-sentence NLI faithfulness check (bare claim bodies)
- Gate 3: same store vs flat-pile context comparison, scope-restricted oracle
  comparisons (same-scope pairs only, fuzzy matching as pre-registered), McNemar
  paired test on the emergent column
- Token sanity: report store/flat ratio (not a gate, threshold ≤ 1.5×)
- Injected column: report separately, does not affect verdict

---

## What Is Fixed by This Pre-Registration

After the 200-article run completes:
- Thresholds cannot be changed
- The scoring method cannot be changed (fuzzy scope matching is locked in)
- The corpus cannot be extended ("more data" is not available after a KILL)
- The oracle cannot be retrained

The output of gates.py on the 200-article corpus, against these thresholds, is
the verdict. Amendments to this document after any 200-article data is seen are
retroactive and therefore invalid.
