# Extractor Robustness Pre-Registration — gpt-4.1-nano vs gpt-4o-mini

**Written:** 2026-06-30
**Status: PRE-REGISTERED — nano extraction has not yet run.**
**This file is read-only and must not be modified after commit.**

---

## Purpose

This run answers one specific question: is the Gate 3 effect (26.5pp emergent
contradiction reduction, McNemar p≈0) an artifact of gpt-4o-mini specifically, or
does it hold across OpenAI extractors?

This run does NOT close the Claude-haiku vs OpenAI confound from the provisional→
confirmatory transition. That gap spans a cross-family difference set aside by choice.
The accurate statement this run can support at best is: "Gate 3 effect holds across
two OpenAI extractors on a fixed corpus."

---

## What Is Held Fixed

- **Corpus:** same 200 articles (Category:Internet protocols + Category:Web standards),
  same edit stream (same old/new revision pairs). No re-fetch. Verify before reporting.
- **Oracle:** same frozen artifact (spike/artifacts/oracle_frozen_20260625T180703/).
  Do not rebuild or retune.
- **Thresholds:** same Gate 3 thresholds (≥20pp AND p<0.05 → PASS; <10pp OR p≥0.05 → KILL).
- **Prompt and schema:** same extraction prompt, same claim structure (body/scope/validity).

**What changes:** extraction model only — gpt-4o-mini → gpt-4.1-nano.

---

## Pre-Registered Verdict Band

Baseline: gpt-4o-mini produced emergent reduction = 26.5pp, McNemar p≈0.0000.

| Result | Label | Interpretation |
|--------|-------|----------------|
| reduction ≥ 20pp AND p < 0.05 | **Robust** | Effect holds across two OpenAI extractors |
| 10pp ≤ reduction < 20pp (any p) | **Partial** | Extractor affects magnitude; directional signal persists |
| reduction < 10pp OR p ≥ 0.05 | **Extractor-dependent** | Effect does not replicate; gpt-4o-mini result is model-specific |

Report the result against this band only. Do not reach for a softer reading if it
lands at, say, 17pp — Partial is a real and honest outcome.

---

## What This Run Does Not Resolve

1. **Gate 1 (hot-node propagation):** Untested across all runs. Max active k=14; k≥25
   needed to probe the stress case. Switching extractors does not move this. The
   hot-node stress test (pre-registered in hot_node_gate1_preregistration.md) is still
   owed and still blocks the §7 build independently.

2. **Overall verdict:** Even a Robust result here leaves the overall state as NO-GO.
   Gate 1's central question is unmeasured. Best-case scorecard after this run:
   Gate 2 PASS, Gate 3 replicated and extractor-robust within OpenAI,
   Gate 1 untested → build still blocked.

3. **Cross-family confound:** The Claude-haiku→gpt-4o-mini swap between the provisional
   and confirmatory runs remains confounded with the corpus change. This run does not
   address that. The accurate framing is: "the cross-family comparison is set aside by
   choice; within the OpenAI family, Gate 3 [holds/partially holds/fails]."
