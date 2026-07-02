# Hot-Node Gate 1 Amendment — Scope Validation, Kill Threshold, No-Hot-Node Outcome

**Written:** 2026-07-02
**Amends:** hot_node_gate1_preregistration.md (same directory)
**Status: PRE-REGISTERED — no fetch, no merge, no k observed yet.**
**This file is read-only and must not be modified after commit.**

---

## Why This Amendment

The original preregistration specified construction methods but did not:
1. Require scope validation before propagation measurement
2. Name the "no hot node achievable" outcome explicitly
3. Specify the article list / fetch strategy before fetching
4. Write kill thresholds in k-independent terms with a sub-linearity check

All four are fixed here, committed blind before any data is seen.

---

## Prerequisite: Scope Validation Before Any Measurement

Token-matching to assemble a candidate cluster risks manufacturing the hot node out
of a bad merge — lumping TCP-the-protocol, TCP-in-app-X, and TCP-congestion-control
into one node. If that happens, Gate 1 measures over-merging artifacts, not real
propagation. The cluster must be validated as a genuine single scope before any
propagation numbers are computed.

### Validation Procedure

**Step 1 — Token-match assembly:**
Collect all active claims from the store whose scope, after stopword removal and
lowercasing, contains ≥2 tokens from the entity's canonical token set (defined below).
This is the raw candidate cluster. Record raw k = number of claims in it.

**Step 2 — Claim-level validation (before seeing oracle output):**
For each claim in the raw cluster, answer one binary question:
> Does this claim's body describe a property of the target entity *as a technical
> specification in its own right* — not as a component, dependency, or side-effect
> of some other system?

Examples for TCP:
- PASS: "TCP uses a three-way handshake to establish connections" — TCP as protocol spec
- PASS: "TCP congestion control reduces send rate on packet loss" — TCP spec property
- FAIL: "QUIC is designed to replace TCP in web transport" — TCP appears but claim is about QUIC
- FAIL: "HTTP/1.1 uses persistent TCP connections by default" — TCP appears but claim is about HTTP

Validation is performed claim-by-claim before running the oracle on any pair.
Record validated k = claims that pass. Record rejected k = raw k − validated k, with reasons.

**Step 3 — Hot-node threshold:**
If validated k ≥ 25: proceed to propagation measurement on the validated cluster only.
If validated k < 25 after fetch (see §Article List): invoke the "No Hot Node" outcome below.

**Report both numbers in the result file:** raw k and validated k, with rejection breakdown.

---

## Kill Threshold (k-independent, set before any k is observed)

Unit: reauth_per_edit (rpe) = oracle-detected within-cluster contradictions / (validated k − 1).
This normalizes by cluster size, making the threshold k-independent.

| Condition | Verdict |
|-----------|---------|
| p95 rpe ≤ 2 across the validated cluster | **PASS** — propagation bounded; hot-node merges are cheap |
| p95 rpe > 10 across the validated cluster | **KILL** — hot-node merges are prohibitively expensive |
| 2 < p95 rpe ≤ 10 | **MARGINAL** — bounded but non-negligible; discuss in §7 scope |

**Rationale:** At validated k=30, p95 rpe=2 means at most 2 re-auths per edit in the
worst-case node — clearly manageable. p95 rpe=10 means 30 re-auths per edit —
prohibitive at scale. These bounds are tighter than the original preregistration
(≤5/≥20) because a genuinely validated hot node is a cleaner measurement surface.

**Sub-linearity check (if ≥2 distinct validated k values are achievable):**
If the corpus or fetch yields clusters at two or more distinct validated k values
(e.g., k=28 and k=40), compute rpe for each and report the slope of rpe ~ k.
- Flat or declining slope → propagation sub-linear → PASS qualifier
- Rising slope (positive, p<0.10) → propagation grows with degree → upgrade MARGINAL to KILL

If only one k value is achievable, report rpe at that k and note the sub-linearity
check was underpowered (one point cannot characterize a trend).

---

## Pre-Registered Article List and Fetch Strategy

**Primary entity: TCP (Transmission Control Protocol)**

Token set for matching: {"tcp", "transmission", "control"} — a scope must contain
≥2 of these tokens after stopword removal to enter the raw candidate cluster.

Targeted fetch (to supplement existing 200-article corpus):
1. All Wikipedia articles whose title contains "TCP" (case-insensitive)
2. All members of Category:Transmission_Control_Protocol (English Wikipedia, cmtype=page)
3. Deduplicate against the existing 200-article corpus (no re-fetch of already-present articles)
4. Apply the same qualification criteria as the main corpus (two real snapshots,
   old ≤ 2021-01-01, both ≥ 800 chars, old ≠ current revid)
5. Fetch up to 40 additional qualifying articles; stop when the list is exhausted

**Secondary entity: HTTP (Hypertext Transfer Protocol)**
Invoked only if TCP yields validated k < 25 after fetch.
Token set: {"http", "hypertext", "transfer"}
Fetch strategy: same as TCP, using Category:Hypertext_Transfer_Protocol, up to 40 articles.

**Stopping rule:** One entity at a time, TCP first. If TCP and HTTP both yield
validated k < 25, invoke the No Hot Node outcome. Do not try a third entity.

**Commit the fetch results (article titles and revids) before running extraction.**

---

## Third Outcome: No Hot Node Achievable

If, after scope validation, no entity reaches validated k ≥ 25 — including after
TCP and HTTP targeted fetches — the verdict is:

> **UNRESOLVABLE: Corpus structurally produces no hot nodes. Propagation risk on
> high-degree entities is unmeasurable on a Wikipedia-shaped workload.**

This is NOT a Gate 1 PASS. It means:
- The hot-node risk has not been cleared — only that this test setup cannot generate
  the stress case needed to test it
- Wikipedia-shaped knowledge does not concentrate enough in any single entity to
  produce k≥25 validated active claims under the extraction settings used
- The real test must move to a corpus where hot nodes are natural: agent memory,
  a codebase (functions called by 50+ files), or an engineering wiki (one shared
  entity like "the auth service" referenced by hundreds of notes)

If this outcome is triggered, it must be reported as a structural finding about
the test setup, not absorbed into a favorable summary. The §7 build remains blocked
until the hot-node question is answered on an appropriate corpus.

---

## What This Amendment Does Not Change

- The frozen oracle (threshold=0.95, DeBERTa-v3-base-mnli-fever-anli) is unchanged
- Gate 2 and Gate 3 results are unchanged
- The overall NO-GO verdict is unchanged pending Gate 1 resolution
- The pre-registered kill threshold from the original preregistration (rpe≤5 PASS,
  rpe>20 KILL) is superseded by the tighter thresholds above (rpe≤2 PASS, rpe>10 KILL),
  because the validation step makes the measurement surface cleaner

---

## Execution Order (must be followed)

1. Commit this file (done when you read this)
2. Fetch TCP articles → commit article list (titles + revids) before extraction
3. Extract claims for new articles using current extractor (gpt-4o-mini)
4. Assemble raw candidate cluster via token-match
5. Validate claims claim-by-claim, record raw k and validated k
6. If validated k ≥ 25: run oracle on within-cluster pairs, compute rpe, apply threshold
7. If validated k < 25 after TCP: repeat steps 2–6 for HTTP
8. If both fail: invoke No Hot Node outcome
9. Write frozen result file
