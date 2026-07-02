# TCP Hot-Node Membership Criterion

**Written:** 2026-07-02
**Status: PRE-REGISTERED — committed before corpus_hotnode.py exists or any claims are seen.**
**This file is read-only and must not be modified after commit.**

---

## Criterion Text (apply verbatim)

> A claim counts as scope=TCP iff its subject is the TCP protocol itself — its
> mechanisms, behavior, properties, or structure. A claim that merely mentions,
> contrasts with, or depends on TCP while being about another entity
> (e.g. "QUIC replaces TCP" → about QUIC; "HTTP/2 runs over TCP" → about HTTP/2)
> does not count.

**SHA-256 of criterion text:**
`8770d0c152b7b5273768727bac4f59511fd7862ffd7acaf938e3eca9c8a7bf9b`

Any script that applies this criterion must embed or load this exact text and verify
the hash at runtime. A mismatch means the criterion has drifted; abort and report.

---

## Implementation (pre-registered)

**Preferred path (chosen):** The membership filter is a second independent LLM pass
using `gpt-4o-mini` with the criterion text above as the system prompt. Each claim
is judged individually (body + scope only; no running k-count visible to the judge).
Claims are shuffled before the validation pass so position carries no signal about
cumulative k.

The primary filter is not a human who knows what answer helps. A human spot-audit
covers ≥20 claims or 20% of the raw cluster (whichever is larger), sampled randomly
after the automated pass completes. The spot-audit result is reported alongside the
automated result; material disagreement (>10% of audited claims) triggers a flag.

**What the judge sees per claim:**
- The criterion text (system prompt)
- scope: <string>
- body: <string>
- Instruction: answer YES (about TCP itself) or NO (about another entity)

**What the judge does not see:**
- The running k count
- Other claims in the cluster
- Which answer is needed for k≥25

---

## Token-Match Rule (raw cluster assembly, before validation)

A claim enters the raw candidate cluster if its scope, after lowercasing and
stopword removal, satisfies:

  contains "tcp"  OR  (contains "transmission" AND contains "control")

This is the only filter applied before the membership criterion above.
Validation then decides which raw-cluster members count toward validated k.

---

## Required Outputs

The result file must report all of the following; none may be omitted:

- raw_k: count of claims passing the token-match rule
- validated_k: count passing the membership criterion
- drop_rate: (raw_k − validated_k) / raw_k
- rejection_breakdown: how many were rejected and the most common rejection reason
- spot_audit_sample_size and spot_audit_agreement_rate

A drop_rate above 50% is itself a finding — it means the token-match surface
is substantially contaminated and the "TCP hot node" is largely a misnomer.
Report it as such, not as a cleanup step.
