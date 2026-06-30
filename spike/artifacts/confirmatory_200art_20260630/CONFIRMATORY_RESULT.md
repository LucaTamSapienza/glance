# Spike 0 — 200-Article Confirmatory Result

**Frozen:** 2026-06-30
**Corpus:** Category:Internet protocols + Category:Web standards (N=200)
**Status: PROVISIONAL — Gate 3 PASS confirmed; extractor-independence unresolved (see §Confounds)**
**This file is read-only and must not be modified after commit.**

---

## Gate Outcomes

| Gate | Verdict | Numbers |
|------|---------|---------|
| Gate 1 — Merge-propagation amplification | **UNREADABLE** | max active k=14; only 2 distinct k values in top-10; no hot nodes produced (see §Gate 1) |
| Gate 2 — View faithfulness | **PASS** | 100/100 violation-free (100%) |
| Gate 3 — Consistency advantage | **PASS** | emergent 26.5pp, McNemar p≈0.0000, b01=28, b10=2; scope_f1=0.746; token ratio 0.62× |

**Pre-registered overall verdict: NO-GO — Gate 1 UNREADABLE blocks §7 build.**

Gate 3 is a replicated, pre-registered positive result. Gate 1's central question — propagation
cost on hot nodes — remains untested because the extractor structurally produced no hot nodes.
An extractor swap mid-spike is an uncontrolled variable that must be closed before any writeup.

---

## Gate 3: Replication Summary

| Run | N | Corpus | Extractor | Emergent reduction | McNemar p |
|-----|---|--------|-----------|--------------------|-----------|
| Spike 0 (original) | 44 | Web browsers | claude-haiku-4-5-20251001 | 18.2pp | 0.077 |
| Provisional | 76 | Web browsers | claude-haiku-4-5-20251001 | 20.3pp | 0.0015 |
| Confirmatory | 200 | Internet protocols + Web standards | gpt-4o-mini | 26.5pp | ≈0.0000 |

The 76→200 comparison is simultaneously a corpus replication and an extractor swap.
These two variables are confounded and cannot be separated from the runs above.
See §Confounds for what this requires.

---

## Gate 1: Third Consecutive UNREADABLE — Structural Finding

Gate 1 has been UNREADABLE across all three runs. This is not an instrument quirk — it
reflects a structural property of the corpus+extractor combination:

**Active k values across all runs:**
- Spike 0 (N=44, Claude): not fully characterized; power-law fit used (now retired)
- Provisional (N=76, Claude): power-law alpha=1.298, CI unstable; raw p95=0.81/edit
- Confirmatory (N=200, GPT-4o-mini): max active k=14, top-10 k ∈ {12, 13, 14}

**Why k stays low:** The store retracts snap0 claims; only snap1 contributes to active k.
With MAX_CLAIMS=12, a scope group reaches k only by appearing as the primary scope
across multiple articles' snap1. At N=200 articles, max active k=14. k≥25 was never
observed in any run.

**What this means:** We have not tested propagation cost on hot nodes. We have tested
propagation cost at k=12–14, where max rpe=0.45 (well below KILL=30). The Gate 1
question — "does a hot node explode?" — is unanswered because the test setup cannot
generate hot nodes. This is a finding about the test setup, not a reassuring datapoint
about propagation at scale.

The re-registered Gate 1 must deliberately construct hot nodes. See
`hot_node_gate1_preregistration.md` in this directory.

---

## Confounds: Extractor Swap Mid-Spike

**Spike 0 and provisional (N=76) used:** `claude-haiku-4-5-20251001` via `claude -p`
**Confirmatory (N=200) used:** `gpt-4o-mini` via OpenAI API

The model was swapped due to Anthropic API quota exhaustion after the 76-article run.
The swap was applied to all 200 confirmatory articles uniformly (not mixed within corpus).

**What is confounded:** The corpus replication (web-browsers → internet-protocols) and
the extractor change (Claude Haiku → GPT-4o-mini) happened in the same transition.
The Gate 3 signal held (18.2pp→20.3pp→26.5pp), but we cannot attribute the direction
or magnitude to the corpus or the extractor independently.

**Required to close:** Re-run Gate 3 on one of the two existing corpora using a second
OpenAI model (distinct from gpt-4o-mini), holding corpus and oracle fixed.
- If Gate 3 holds: extractor-independence established within the OpenAI family; the
  Claude→OpenAI swap becomes a noted limitation, not a blocking confound.
- If Gate 3 moves materially: extractor is part of the effect; finding must be stated
  conditionally on extractor choice.

The extractor-comparison run uses the same frozen oracle, same thresholds, same corpus
(200-article internet-protocols + web-standards). It is not a new gate run — it is a
robustness check on an existing PASS. Its result does not change the pre-registered
NO-GO verdict; it determines how the finding can be stated in a writeup.

---

## Store Statistics (200-article run)

- Total claims extracted: 4779
- In store: 4145 (total), 1756 active, 634 superseded, 1755 retracted, 0 conflicts
- Candidate merges detected: 21
- Scope F1 (fuzzy matching): 0.746
- Token ratio (store / flat): 0.62×
- Extraction model: gpt-4o-mini (OpenAI API, temperature=0)
