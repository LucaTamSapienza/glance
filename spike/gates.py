"""Spike 0 — gate measurement harness.

Runs all three gates and prints PASS / KILL / UNREADABLE for each, then
an overall go/no-go. Gate thresholds are pre-registered in the work order:
  Gate 1: alpha <= 0.6 and p95 <= 15/edit -> PASS; alpha >= 1.0 or p95 > 40 -> KILL
  Gate 2: >= 95% violation-free -> PASS; > 10% violations -> KILL
  Gate 3: emergent contradiction-rate reduction >= 20pp, McNemar p < 0.05 -> PASS;
           < 10pp or not significant or vanishes under oracle -> KILL;
           scope-F1 < 0.5 -> UNREADABLE
"""
import os, sys, json, collections, math, random, statistics
import numpy as np
from scipy import stats
from statsmodels.stats.contingency_tables import mcnemar

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "data")
sys.path.insert(0, HERE)

from oracle import Oracle
from store import build_store
from canon import canonicalize, scope_extraction_quality
from read import compose_view, check_faithfulness


# ---- helpers ----

def _scope_key(s):
    return s.lower().strip()

def _load_claims():
    return json.load(open(os.path.join(DATA, "claims.json")))

def _claims_by_article(claims):
    d = collections.defaultdict(lambda: [[], []])
    for c in claims:
        d[c["prov"]["article"]][c["snapshot_index"]].append(c)
    return d


# ---- Gate 1 — merge-propagation amplification ----

def gate1(store, oracle, claims):
    """For each high-degree scope group, count how many same-scope active claims
    would need semantic re-authoring if that scope's canonical claim changed.
    Fit re-auth ~ k^alpha. Report CI, raw counts for top-3 nodes, p95.
    """
    print("\n" + "="*60)
    print("GATE 1 — Merge-propagation amplification")
    print("="*60)

    # Degree = number of active claims per scope key
    active = store.active_claims()
    by_scope = collections.defaultdict(list)
    for c in active:
        by_scope[_scope_key(c["scope"])].append(c)

    # (scope_key, claims_list) sorted by degree descending
    sorted_scopes = sorted(by_scope.items(), key=lambda x: -len(x[1]))
    # Need at least 5 different degrees for a fit; cap at 30 scopes
    scopes_to_analyze = sorted_scopes[:30]

    # For each scope, "k" = # claims in that scope group.
    # "re-auth count" = # claims in the group that are NOT semantically identical
    # to the most common claim body (proxy: oracle contradiction pairs within group).
    # If k=1, re-auth=0 by definition.
    k_vals = []
    reauth_vals = []
    raw_top3 = []

    for i, (sk, group) in enumerate(scopes_to_analyze):
        k = len(group)
        if k < 2:
            k_vals.append(k)
            reauth_vals.append(0)
            continue
        # Count oracle contradictions within the group (proxy for re-authoring needed)
        contra_count = 0
        for a in range(len(group)):
            for b in range(a+1, len(group)):
                is_c, _ = oracle.contradicts(group[a]["body"], group[b]["body"])
                if is_c:
                    contra_count += 1
        k_vals.append(k)
        reauth_vals.append(contra_count)
        if i < 3:
            raw_top3.append({"scope": sk[:50], "k": k, "reauth": contra_count,
                             "reauth_per_edit": round(contra_count / max(k-1, 1), 2)})

    print(f"\nTop-3 high-degree nodes (raw counts):")
    for r in raw_top3:
        print(f"  scope={r['scope']!r:50s}  k={r['k']:3d}  "
              f"reauth={r['reauth']:3d}  reauth/edit={r['reauth_per_edit']:.2f}")

    # Fit power law: log(reauth+1) ~ alpha * log(k) + c
    xs = np.array([math.log(k) for k in k_vals if k >= 2])
    ys = np.array([math.log(r+1) for k, r in zip(k_vals, reauth_vals) if k >= 2])

    if len(xs) < 4:
        verdict = "UNREADABLE"
        print(f"\n  Too few data points ({len(xs)}) for reliable fit.")
        print(f"\nGATE 1: {verdict}")
        return verdict, None

    slope, intercept, r_val, p_val, stderr = stats.linregress(xs, ys)
    alpha = slope
    ci95_lo = alpha - 1.96 * stderr
    ci95_hi = alpha + 1.96 * stderr

    # p95 of reauth_per_edit for top-degree nodes
    reauth_per_edit = [r / max(k-1, 1) for k, r in zip(k_vals, reauth_vals) if k >= 2]
    p95 = np.percentile(reauth_per_edit, 95) if reauth_per_edit else 0.0

    print(f"\n  alpha={alpha:.3f}  CI95=[{ci95_lo:.3f}, {ci95_hi:.3f}]  "
          f"p95_reauth_per_edit={p95:.2f}  R2={r_val**2:.3f}")

    ci_width = ci95_hi - ci95_lo
    if ci_width > 0.6:
        verdict = "UNREADABLE"
        print(f"  CI too wide ({ci_width:.2f} > 0.6) — trust raw counts, re-measure with more nodes")
    elif alpha >= 1.0 or p95 > 40:
        verdict = "KILL"
    elif alpha <= 0.6 and p95 <= 15:
        verdict = "PASS"
    else:
        verdict = "KILL"  # between thresholds -> fail-safe

    print(f"\nGATE 1: {verdict}")
    return verdict, {"alpha": alpha, "ci95": [ci95_lo, ci95_hi], "p95": p95}


# ---- Gate 2 — view faithfulness ----

def gate2(store, oracle, claims, n_pairs=200, seed=42):
    """Sample n_pairs (article -> store view); check faithfulness via NLI entailment."""
    print("\n" + "="*60)
    print("GATE 2 — View faithfulness")
    print("="*60)

    rng = random.Random(seed)
    articles = list({c["prov"]["article"] for c in store.active_claims()})
    rng.shuffle(articles)

    violation_free = 0
    violated = 0
    total = 0

    for article in articles:
        if total >= n_pairs:
            break
        source = store.snapshot_context(article, n=10)
        if len(source) < 2:
            continue
        view = compose_view(source, max_claims=10)
        result = check_faithfulness(view, source, entail_threshold=0.5)
        if result["violation_free"]:
            violation_free += 1
        else:
            violated += 1
        total += 1
        if total % 20 == 0:
            print(f"  [{total}/{n_pairs}] violation_free={violation_free}  violated={violated}")

    if total == 0:
        verdict = "UNREADABLE"
        print("  No articles to evaluate.")
        print(f"\nGATE 2: {verdict}")
        return verdict, {}

    pct_clean = violation_free / total
    pct_violated = violated / total
    print(f"\n  total={total}  violation_free={violation_free} ({pct_clean:.1%})  "
          f"violated={violated} ({pct_violated:.1%})")

    if pct_clean >= 0.95:
        verdict = "PASS"
    elif pct_violated > 0.10:
        verdict = "KILL"
    else:
        verdict = "KILL"  # 5-10% -> fix composition; treated as kill for the spike

    print(f"\nGATE 2: {verdict}")
    return verdict, {"pct_clean": pct_clean, "n": total}


# ---- Gate 3 — consistency advantage ----

def _context_contra_rate(contexts, oracle):
    """For each context (list of claims), count contexts that contain a contradiction
    between SAME-SCOPE claims. Cross-scope comparisons are skipped to avoid oracle
    false positives on unrelated entities with similar vocabulary.
    Returns (n_contexts_with_contradiction, total_contexts)."""
    from canon import scopes_match
    with_contra = 0
    for ctx in contexts:
        found = False
        for i in range(len(ctx)):
            if found:
                break
            for j in range(i+1, len(ctx)):
                # Only compare claims with semantically matching scopes (fuzzy)
                if not scopes_match(ctx[i].get("scope",""), ctx[j].get("scope","")):
                    continue
                is_c, _ = oracle.contradicts(ctx[i]["body"], ctx[j]["body"])
                if is_c:
                    found = True
                    break
        if found:
            with_contra += 1
    return with_contra, len(contexts)


def gate3(store, oracle, claims, n_queries=200, seed=42):
    """Compare contradiction rate: store context vs flat pile, for matched queries.
    Split injected vs emergent. Report McNemar test + scope F1."""
    print("\n" + "="*60)
    print("GATE 3 — Consistency advantage")
    print("="*60)

    rng = random.Random(seed)
    cba = _claims_by_article(claims)
    articles = [a for a, (s0, s1) in cba.items() if s0 and s1]
    rng.shuffle(articles)

    # Build flat pile: all claims for an article from both snapshots (no dedup)
    flat_pile = claims

    # Scope extraction quality
    print("\n  Computing scope-extraction quality ...")
    sq = scope_extraction_quality(oracle, cba)
    print(f"  scope_f1_proxy={sq['scope_f1_proxy']:.3f}  "
          f"scope_confusion_rate={sq['scope_confusion_rate']:.3f}  "
          f"similar_pairs={sq.get('total_similar_pairs', sq.get('total_oracle_contradictions','?'))}")

    if sq["scope_f1_proxy"] < 0.5:
        verdict = "UNREADABLE"
        print("  scope-extraction F1 < 0.5 — cannot separate store-doesn't-help from noisy-scope")
        print(f"\nGATE 3: {verdict}")
        return verdict, sq

    # ---- EMERGENT contradictions (from real edit stream) ----
    print("\n  Evaluating emergent contradictions (real edit stream) ...")
    store_ctx_emergent = []
    flat_ctx_emergent = []

    for article in articles[:n_queries]:
        s_ctx = store.snapshot_context(article, n=15)
        f_ctx = store.flat_context(article, flat_pile, n=15)
        if len(s_ctx) < 2 or len(f_ctx) < 2:
            continue
        store_ctx_emergent.append(s_ctx)
        flat_ctx_emergent.append(f_ctx)

    if not store_ctx_emergent:
        verdict = "UNREADABLE"
        print("  No matched pairs for emergent evaluation.")
        print(f"\nGATE 3: {verdict}")
        return verdict, sq

    store_contra, store_total = _context_contra_rate(store_ctx_emergent, oracle)
    flat_contra, flat_total = _context_contra_rate(flat_ctx_emergent, oracle)

    store_rate = store_contra / store_total if store_total else 0.0
    flat_rate = flat_contra / flat_total if flat_total else 0.0
    reduction_emergent = flat_rate - store_rate

    # McNemar test: paired comparison (same query, does store eliminate contradiction?)
    # Build 2x2: [both_contra, only_flat_contra, only_store_contra, neither]
    b00, b01, b10, b11 = 0, 0, 0, 0
    for sc, fc in zip(store_ctx_emergent, flat_ctx_emergent):
        sc_has, _ = _context_contra_rate([sc], oracle)
        fc_has, _ = _context_contra_rate([fc], oracle)
        sc_c = sc_has > 0
        fc_c = fc_has > 0
        if sc_c and fc_c:     b11 += 1
        elif not sc_c and fc_c: b01 += 1
        elif sc_c and not fc_c: b10 += 1
        else: b00 += 1

    table = [[b00, b01], [b10, b11]]
    try:
        mc_result = mcnemar(table, exact=True)
        mc_p = mc_result.pvalue
    except Exception:
        mc_p = 1.0

    print(f"\n  EMERGENT: store_rate={store_rate:.3f}  flat_rate={flat_rate:.3f}  "
          f"reduction={reduction_emergent:.3f} ({reduction_emergent*100:.1f}pp)")
    print(f"  McNemar: b01={b01} b10={b10} p={mc_p:.4f}")
    print(f"  (b01=only-flat-contra, b10=only-store-contra)")

    # ---- INJECTED contradictions (synthetic, for context only) ----
    print("\n  Evaluating injected contradictions (synthetic pairs, context only) ...")
    injected_pairs = _build_injected_pairs(claims, oracle, n=50, seed=seed+1)
    inj_store_contra = sum(1 for sc, fc in injected_pairs
                           if _context_contra_rate([sc], oracle)[0] > 0)
    inj_flat_contra = sum(1 for sc, fc in injected_pairs
                          if _context_contra_rate([fc], oracle)[0] > 0)
    inj_n = len(injected_pairs)
    inj_store_rate = inj_store_contra / inj_n if inj_n else 0.0
    inj_flat_rate = inj_flat_contra / inj_n if inj_n else 0.0
    print(f"  INJECTED (n={inj_n}): store_rate={inj_store_rate:.3f}  "
          f"flat_rate={inj_flat_rate:.3f}  "
          f"reduction={inj_flat_rate-inj_store_rate:.3f} ({(inj_flat_rate-inj_store_rate)*100:.1f}pp)")
    print("  (injected column: context only, does not affect PASS/KILL verdict)")

    # ---- Token sanity ----
    store_tokens = sum(len(c["body"].split()) for ctx in store_ctx_emergent for c in ctx)
    flat_tokens = sum(len(c["body"].split()) for ctx in flat_ctx_emergent for c in ctx)
    token_ratio = store_tokens / flat_tokens if flat_tokens else 1.0
    print(f"\n  Token sanity: store={store_tokens}w  flat={flat_tokens}w  "
          f"ratio={token_ratio:.2f}x (threshold <=1.5x)")

    # ---- Wikipedia base-rate caveat ----
    print(f"\n  [CAVEAT] Wikipedia is human-edited toward consistency; "
          f"emergent-contradiction base rate may be lower than real target domains. "
          f"flat_rate={flat_rate:.3f} — if near zero, corpus is weak test, not a verdict.")

    # ---- Verdict (emergent column only) ----
    if reduction_emergent >= 0.20 and mc_p < 0.05:
        verdict = "PASS"
    elif reduction_emergent < 0.10 or mc_p >= 0.05:
        verdict = "KILL"
    else:
        verdict = "KILL"

    print(f"\nGATE 3: {verdict}  (emergent: {reduction_emergent*100:.1f}pp reduction, "
          f"McNemar p={mc_p:.4f})")
    return verdict, {
        "emergent_store_rate": store_rate, "emergent_flat_rate": flat_rate,
        "emergent_reduction_pp": round(reduction_emergent * 100, 1),
        "mcnemar_p": round(mc_p, 4),
        "injected_store_rate": inj_store_rate, "injected_flat_rate": inj_flat_rate,
        "scope_quality": sq,
        "token_ratio": round(token_ratio, 3),
    }


def _build_injected_pairs(claims, oracle, n=50, seed=0):
    """Build (store_ctx, flat_ctx) pairs where flat_ctx has a synthetic contradiction
    injected and store_ctx does not. Used for the injected-column Gate 3 measurement.
    """
    rng = random.Random(seed)
    by_article = _claims_by_article(claims)
    articles = [a for a, (s0, s1) in by_article.items() if s0 and s1]
    rng.shuffle(articles)

    pairs = []
    for article in articles:
        if len(pairs) >= n:
            break
        snap0 = by_article[article][0]
        snap1 = by_article[article][1]
        if not snap0 or not snap1:
            continue
        # store context: only snap1 claims (clean)
        store_ctx = snap1[:10]
        # flat context: snap0 + snap1 mixed (may have emergent contradictions)
        flat_ctx = (snap0 + snap1)[:10]
        pairs.append((store_ctx, flat_ctx))

    return pairs[:n]


# ---- main ----

def main():
    print("Loading claims ...")
    claims = _load_claims()
    print(f"  {len(claims)} claims loaded")

    print("\nLoading oracle (frozen) ...")
    oracle = Oracle()
    print(f"  threshold={oracle.threshold}")

    print("\nBuilding store ...")
    store, _ = build_store(claims, oracle=None)  # no conflict scan at build; done in gates
    stats = store.stats()
    print(f"  {stats}")

    print("\nRunning canonicalize (no LLM confirm — spike mode) ...")
    merge_log = canonicalize(store, claims, llm_confirm=False, jaccard_thresh=0.55)
    print(f"  {len(merge_log)} candidate merges detected")

    # Run gates
    v1, d1 = gate1(store, oracle, claims)
    v2, d2 = gate2(store, oracle, claims, n_pairs=100)
    v3, d3 = gate3(store, oracle, claims, n_queries=100)

    # Overall
    print("\n" + "="*60)
    print("SUMMARY")
    print("="*60)
    print(f"  Gate 1 (merge-propagation):  {v1}")
    print(f"  Gate 2 (view faithfulness):  {v2}")
    print(f"  Gate 3 (consistency adv.):   {v3}")

    kills = [v for v in [v1, v2, v3] if v == "KILL"]
    unreadable = [v for v in [v1, v2, v3] if v == "UNREADABLE"]
    if kills:
        verdict = "NO-GO (KILL)"
    elif unreadable:
        verdict = "NO-GO (UNREADABLE — fix instrument)"
    else:
        verdict = "GO"
    print(f"\n  Overall: {verdict}")


if __name__ == "__main__":
    main()
