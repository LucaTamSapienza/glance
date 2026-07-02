# Extractor Robustness Result — gpt-4.1-nano vs gpt-4o-mini

**Frozen:** 2026-07-02
**Status: FROZEN — do not modify after commit.**

---

## Gate 3: Partial Replication (pre-registered band: 10–20pp)

Same corpus (200-article internet protocols + web standards), same frozen oracle,
same edit stream. Extractor changed from gpt-4o-mini to gpt-4.1-nano.

| Extractor | Claims | Active | store_rate | flat_rate | Reduction | b01 | b10 | McNemar p |
|-----------|--------|--------|------------|-----------|-----------|-----|-----|-----------|
| gpt-4o-mini | 4779 | 1756 | 0.327 | 0.592 | **26.5pp** | 28 | 2 | ≈0.0000 |
| gpt-4.1-nano | 4733 | 1821 | 0.505 | 0.657 | **15.2pp** | 25 | 10 | 0.0167 |

**Verdict against pre-registered band:** PARTIAL — reduction 15.2pp (10–20pp range),
p=0.017 (significant). Direction and significance replicate; magnitude does not.

The effect is real and directionally consistent across both extractors. The magnitude
difference (26.5pp vs 15.2pp) is mechanistically explained: nano produces a higher
store_rate (0.505 vs 0.327), meaning the store context contains more residual
contradiction under nano extraction. This is extraction quality, not oracle noise —
nano extracts less precisely scoped claims, so the store's deduplication/supersession
step is less effective at eliminating contradictions. The reduction is smaller because
nano starts from a noisier baseline.

**This is a finding in its own right:** Gate 3's magnitude is extraction-quality-
sensitive. A store built on high-quality scoped claims removes more contradiction
(26.5pp) than one built on lower-quality claims (15.2pp), but both are significant
reductions over flat-pile context. The benefit scales with extraction precision.
This must be stated explicitly in any writeup, not absorbed into a single number.

---

## What This Run Does and Does Not Establish

**Establishes:** Gate 3 effect is robust in direction and significance across two OpenAI
extractors on a fixed corpus. The consistency advantage is not an artifact of gpt-4o-mini.

**Does not establish:** Cross-family extractor independence. The Claude-haiku (76-art
provisional) vs OpenAI (200-art confirmatory) comparison is confounded with the corpus
change and set aside by choice. The accurate statement is: "Gate 3 replicates in
direction and significance across two OpenAI extractors; the Claude-vs-OpenAI comparison
remains confounded and is not resolved here."

**Does not change the verdict:** Overall state remains NO-GO. Gate 1's hot-node
propagation question is the binding constraint, independent of this result.

---

## Gate 1: PASS Verdict Rejected — Still Untested

The gate script returned PASS based on OLS trend (slope=-0.015, p=0.87) over k=10–13.
This verdict is not accepted.

Max active k this run: 13. The pre-registered hot-node threshold is k≥25. A flat OLS
trend across k=10–13 says nothing about propagation at k=50. The instrument change
(power-law → OLS trend) produced a verdictable output, but the underlying condition
that made all prior runs UNREADABLE has not changed: no hot nodes exist in the corpus.

**Gate 1 remains untested.** The hot-node stress test (pre-registered in
hot_node_gate1_preregistration.md) is still owed. The OLS PASS is an instrument
artifact of measuring a flat region of a curve whose critical regime was never reached.

---

## Updated Scorecard (all runs)

| Run | N | Corpus | Extractor | Gate 1 | Gate 2 | Gate 3 | Overall |
|-----|---|--------|-----------|--------|--------|--------|---------|
| Spike 0 | 44 | Web browsers | claude-haiku | UNREADABLE (power-law) | PASS | KILL (18.2pp p=0.077) | NO-GO |
| Provisional | 76 | Web browsers | claude-haiku | UNREADABLE (alpha unstable) | PASS | PASS (20.3pp p=0.0015) | PROVISIONAL |
| Confirmatory | 200 | Internet protocols | gpt-4o-mini | UNREADABLE (k=14, 2 distinct) | PASS | PASS (26.5pp p≈0) | NO-GO |
| Extractor check | 200 | Internet protocols | gpt-4.1-nano | UNTESTED (k=13, OLS PASS rejected) | PASS | PARTIAL (15.2pp p=0.017) | NO-GO |

**Binding constraint on §7 build:** Gate 1 — hot-node propagation cost unmeasured
across all four runs. Max active k has never exceeded 14 in any configuration.

---

## What Remains Owed

1. **Hot-node Gate 1 (pre-registered):** Construct an entity with k≥25 active claims,
   force merge/split, report worst-case rpe against pre-set threshold (rpe≤5 PASS,
   rpe>20 KILL). See hot_node_gate1_preregistration.md.

2. **Gate 3 extraction-sensitivity statement:** The magnitude dependence on extraction
   quality (15–26pp range across extractors) is a result to be stated, not a
   nuisance to be footnoted. It implies the store's contradiction-reduction benefit
   is partially a function of how well the extractor scopes claims.
