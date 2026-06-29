# Spike 0 — 76-Article Provisional Result

**Frozen:** 2026-06-29  
**Status: PROVISIONAL — N=76, not the pre-registered ~200. See N explanation below.**  
**This file is read-only and must not be modified after commit.**

---

## Why N=76 Instead of ~200

The pre-registration committed to "~200 articles from Category:Web browsers."
The hard facts from the fetch:

- Category:Web browsers contains **99 total page-type members** (confirmed via API;
  `cmtype=page` with `cmlimit=500`, no continuation — the category is fully enumerated)
- Of those 99 candidates, **76 qualified** under the corpus criteria:
  two real revision snapshots, old revision ≤ 2021-01-01, both snapshots ≥ 800 chars,
  old ≠ current revid
- **23 were skipped**: articles too short in one or both snapshots (< 800 chars), or
  whose oldest-available revision post-dated 2021-01-01 (no old snapshot available),
  or whose old and current revid were identical (no substantive edit history in the window)

This is a **hard fetch constraint**, not optional stopping. The category does not
contain 200 qualifying articles. The corpus was not stopped early at a convenient result.

**The consequence for the pre-registration:** N=76 is the achievable maximum for this
category and these qualification criteria. The registered ~200 was based on a
mistaken assumption about category size. The confirmatory run must use a
non-overlapping article set — see amended pre-registration for the resolution.

---

## Gate Outcomes (76-article run, pre-registered thresholds)

| Gate | Verdict | Numbers |
|------|---------|---------|
| Gate 1 — Merge-propagation amplification | **UNREADABLE** | alpha=1.298, CI95=[0.640, 1.955], CI width=1.31; raw p95=0.81/edit; top-3: 0.36/0.82/0.40 reauth/edit |
| Gate 2 — View faithfulness | **PASS** | 74/74 violation-free (100%) |
| Gate 3 — Consistency advantage | **PASS (provisional)** | emergent 20.3pp, McNemar p=0.0015, b01=18, b10=3; scope_f1=0.776; token ratio 0.75× |

**Overall: PROVISIONAL — Gate 3 PASS is real, Gate 1 is contradictory with Spike 0.**

---

## Gate 1: Why "UNREADABLE" Understates the Problem

Between the 44-article Spike 0 run and this 76-article run, alpha moved from 0.58
to 1.30 — crossing the entire decision range in one corpus increment. The CI widened
rather than narrowed as N increased. This is not merely underpowered: it is an
**unstable instrument** whose output is not converging. Reporting it as UNREADABLE and
noting "raw p95=0.81 is reassuring" selectively picks the reassuring number. The
wandering exponent is the finding; it means the power-law fit is the wrong instrument
at this corpus scale.

The raw numbers ARE encouraging (max reauth/edit=0.82 across 76 articles, well below
the KILL threshold of 40), but they must be reported as a direct measurement, not
laundered through a fit that is contradicting itself. The amended pre-registration
replaces the power-law fit with a direct degree-vs-reauth curve; see that document
for the new instrument and its pre-registered thresholds.

---

## Gate 3: PASS Provisional — Two Conditions to Confirm

Gate 3 crossed both thresholds: 20.3pp > 20pp, p=0.0015 < 0.05. The signal is
consistent across both runs (Spike 0: 18.2pp p=0.077; this run: 20.3pp p=0.0015).
b01=18 vs b10=3 is a strong directional signal.

This remains **provisional** because:
1. N=76 was not the pre-registered size (~200), so the stopping rule was not followed
2. The confirmatory run on a non-overlapping article set has not yet been executed

If the confirmatory run also clears Gate 3 (≥20pp and p<0.05), the finding is
confirmed across two independent corpora and is publishable on those grounds.

---

## Store Statistics (76-article run)

- Total claims: 1780 extracted, 1733 in store
- Active: 840, Superseded: 46, Retracted: 847
- Candidate merges detected (Jaccard ≥ 0.55, no LLM confirm): 12
- Scope F1 (fuzzy matching, pre-registered): 0.776
- Token ratio (store / flat): 0.75×
