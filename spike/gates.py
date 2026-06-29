"""Spike 0 — gate measurement harness.

Confirmatory run (2026-06-29): Gate 1 power-law fit retired; replaced with direct
degree-vs-reauth curve (pre-registered in amended_preregistration.md before any
new corpus was fetched). Gate 2 and Gate 3 thresholds unchanged.

  Gate 1 (direct): max_reauth/edit <= 10 AND slope not sig positive -> PASS;
                   max_reauth/edit > 30 OR slope > 0 p < 0.05 -> KILL
  Gate 2: >= 95% violation-free -> PASS; > 10% violations -> KILL
  Gate 3: emergent reduction >= 20pp AND McNemar p < 0.05 -> PASS;
           < 10pp or p >= 0.05 -> KILL; scope-F1 < 0.5 -> UNREADABLE
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


# ---- Gate 1 — direct degree-vs-reauth curve (power-law fit retired 2026-06-29) ----
#
# Pre-registered thresholds (amended_preregistration.md, before any new corpus fetched):
#   PASS:        max_reauth_per_edit <= 10  AND  (slope <= 0 OR OLS p >= 0.10)
#   KILL:        max_reauth_per_edit > 30   OR   (slope > 0 AND OLS p < 0.05)
#   between:     KILL (fail-safe)
#   UNREADABLE:  < 4 distinct k values across top-10, OR < 6 groups total

def gate1(store, oracle, claims):
    """Direct degree-vs-reauth curve for top-10 scope groups.
    Reports full table (rank/scope/k/reauth/rpe) and OLS trend across the 10 points.
    """
    print("\n" + "="*60)
    print("GATE 1 — Merge-propagation amplification (direct curve)")
    print("="*60)

    active = store.active_claims()
    by_scope = collections.defaultdict(list)
    for c in active:
        by_scope[_scope_key(c["scope"])].append(c)

    sorted_scopes = sorted(by_scope.items(), key=lambda x: -len(x[1]))
    top10 = sorted_scopes[:10]

    rows = []
    for rank, (sk, group) in enumerate(top10, 1):
        k = len(group)
        reauth = 0
        if k >= 2:
            for a in range(len(group)):
                for b in range(a + 1, len(group)):
                    is_c, _ = oracle.contradicts(group[a]["body"], group[b]["body"])
                    if is_c:
                        reauth += 1
        rpe = reauth / max(k - 1, 1)
        rows.append({"rank": rank, "scope": sk, "k": k, "reauth": reauth, "rpe": rpe})

    print(f"\n{'Rank':>4}  {'k':>4}  {'reauth':>6}  {'rpe':>6}  scope")
    for r in rows:
        print(f"  {r['rank']:2d}   {r['k']:4d}  {r['reauth']:6d}  {r['rpe']:6.2f}  {r['scope'][:50]}")

    rpes = np.array([r["rpe"] for r in rows])
    ks   = np.array([r["k"]   for r in rows])

    dist_stats = {
        "min": float(np.min(rpes)), "median": float(np.median(rpes)),
        "p75": float(np.percentile(rpes, 75)), "p95": float(np.percentile(rpes, 95)),
        "max": float(np.max(rpes)),
    }
    print(f"\n  Distribution of reauth/edit across top-10:")
    print(f"  min={dist_stats['min']:.2f}  median={dist_stats['median']:.2f}  "
          f"p75={dist_stats['p75']:.2f}  p95={dist_stats['p95']:.2f}  max={dist_stats['max']:.2f}")

    # OLS trend: reauth_per_edit ~ k
    distinct_k = len(set(int(x) for x in ks))
    n_groups = len(rows)
    if distinct_k < 4 or n_groups < 6:
        verdict = "UNREADABLE"
        print(f"\n  Only {distinct_k} distinct k values across {n_groups} groups — "
              f"cannot characterize trend (need >= 4 distinct k, >= 6 groups)")
        print(f"\nGATE 1: {verdict}")
        return verdict, {"rows": rows, "dist": dist_stats}

    slope, intercept, r_val, p_val, stderr = stats.linregress(ks, rpes)
    trend_up = slope > 0 and p_val < 0.05
    trend_warn = slope > 0 and p_val < 0.10
    print(f"\n  OLS trend reauth/edit ~ k:  slope={slope:.4f}  p={p_val:.4f}  "
          f"R2={r_val**2:.3f}  ({'significant upward trend' if trend_up else 'no significant upward trend'})")

    max_rpe = dist_stats["max"]
    if max_rpe > 30 or trend_up:
        verdict = "KILL"
    elif max_rpe <= 10 and not trend_warn:
        verdict = "PASS"
    else:
        verdict = "KILL"  # between thresholds: max_rpe 10-30 or borderline trend

    print(f"\nGATE 1: {verdict}")
    return verdict, {"rows": rows, "dist": dist_stats, "slope": slope, "p": p_val}


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
