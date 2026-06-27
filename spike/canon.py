"""Spike 0 — scope-aware candidate merge detection.

Finds claims that are likely duplicates (same scope, high body similarity via
Jaccard shingling) and optionally confirms via LLM (claude -p). Merges are
reversible: the old claim is superseded, not deleted. This module also measures
scope-extraction quality (Gate 3 input) by counting scope-distinct vs scope-matched
contradictions as a proxy for scope-confusion error rate.
"""
import os, json, re, subprocess, tempfile, collections

HERE = os.path.dirname(os.path.abspath(__file__))
MODEL = "claude-haiku-4-5-20251001"
TMP = tempfile.gettempdir()

# Jaccard similarity on 3-char shingles
def _shingles(text, k=3):
    t = text.lower()
    return set(t[i:i+k] for i in range(len(t) - k + 1))

def jaccard(a, b, k=3):
    sa, sb = _shingles(a, k), _shingles(b, k)
    if not sa or not sb:
        return 0.0
    return len(sa & sb) / len(sa | sb)

_STOPWORDS = {"the", "a", "an", "of", "in", "for", "and", "on", "to", "by",
              "web", "browser", "browsers", "users", "list", "table"}

def _scope_key(scope):
    return scope.lower().strip()

def _scope_tokens(scope):
    """Normalize scope to a frozenset of significant tokens for fuzzy matching."""
    words = re.sub(r"[^a-z0-9 ]", " ", scope.lower()).split()
    return frozenset(w for w in words if w and w not in _STOPWORDS) or frozenset(words)

def scopes_match(s0, s1, token_f1_thresh=0.5):
    """Return True if scope strings are equivalent (exact key match or token F1 >= thresh)."""
    if _scope_key(s0) == _scope_key(s1):
        return True
    t0, t1 = _scope_tokens(s0), _scope_tokens(s1)
    if not t0 or not t1:
        return False
    inter = len(t0 & t1)
    if inter == 0:
        return False
    p = inter / len(t1)
    r = inter / len(t0)
    f1 = 2 * p * r / (p + r)
    return f1 >= token_f1_thresh


def candidate_pairs(claims, jaccard_thresh=0.5):
    """Find (ci, cj) pairs with same scope key and body Jaccard >= thresh."""
    by_scope = collections.defaultdict(list)
    for c in claims:
        by_scope[_scope_key(c["scope"])].append(c)
    pairs = []
    for sk, group in by_scope.items():
        for i in range(len(group)):
            for j in range(i + 1, len(group)):
                ci, cj = group[i], group[j]
                if ci["id"] == cj["id"]:
                    continue
                j_score = jaccard(ci["body"], cj["body"])
                if j_score >= jaccard_thresh:
                    pairs.append((ci, cj, j_score))
    return sorted(pairs, key=lambda x: -x[2])


def llm_confirm_merge(ca, cb):
    """Ask LLM whether two claims are semantically equivalent and should be merged.
    Returns True/False. Uses claude -p CLI."""
    prompt = (
        f'Two claims about "{ca["scope"]}":\n'
        f'A: {ca["body"]}\n'
        f'B: {cb["body"]}\n\n'
        f'Are these claims semantically equivalent (same fact, same scope, same validity)? '
        f'Reply with exactly one word: YES or NO.'
    )
    try:
        r = subprocess.run(["claude", "-p", "--model", MODEL, prompt],
                           capture_output=True, text=True, timeout=60, cwd=TMP)
        answer = r.stdout.strip().upper()
        return answer.startswith("YES")
    except Exception:
        return False


def canonicalize(store, claims, llm_confirm=False, jaccard_thresh=0.5):
    """Detect candidate duplicate claims and (optionally) merge them in the store.
    Returns merge_log: list of {kept_id, dropped_id, jaccard, llm_confirmed, scope}.
    Merges are applied to the store: dropped claim is superseded by kept claim.
    """
    active = store.active_claims()
    pairs = candidate_pairs(active, jaccard_thresh=jaccard_thresh)
    merge_log = []
    merged_ids = set()

    for ca, cb, j_score in pairs:
        if ca["id"] in merged_ids or cb["id"] in merged_ids:
            continue
        confirmed = True
        if llm_confirm:
            confirmed = llm_confirm_merge(ca, cb)
        if confirmed:
            # keep ca (older / first seen), supersede with ca (no-op body change)
            # the merge is: mark cb superseded, add its provenance to ca's record
            store.supersede(cb["id"], {**ca,
                "prov": {"merged_from": [ca["prov"], cb["prov"]],
                         "article": ca["prov"]["article"]}})
            merged_ids.add(cb["id"])
            merge_log.append({
                "kept_id": ca["id"], "dropped_id": cb["id"],
                "jaccard": round(j_score, 3), "llm_confirmed": confirmed,
                "scope": ca["scope"],
            })
    return merge_log


def scope_extraction_quality(oracle, claims_by_article):
    """Estimate scope-extraction consistency as a Gate 3 input.

    Method: among claim pairs from the same article, different snapshots, that
    have high body Jaccard similarity (Jaccard >= 0.35 -> likely same entity,
    different wording or temporal update), what fraction receive the same scope key?

    A low score means the LLM assigns inconsistent scope strings to the same
    entity across snapshots, which poisons same-scope contradiction detection.

    NOTE: We do NOT use the oracle here because oracle fires on cross-entity
    surface-level "contradictions" (e.g. "Tor is Gecko-based" vs "Pale Moon
    is Goanna-based") — those are scope-DISTINCT true facts, not scope
    confusion errors. Using oracle to measure scope quality conflates the two.
    """
    same_scope_count = 0
    diff_scope_count = 0

    for article, (snap0, snap1) in claims_by_article.items():
        for c0 in snap0:
            for c1 in snap1:
                if c0["id"] == c1["id"]:
                    continue
                j = jaccard(c0["body"], c1["body"], k=4)
                if j < 0.35:
                    continue
                if scopes_match(c0["scope"], c1["scope"]):
                    same_scope_count += 1
                else:
                    diff_scope_count += 1

    total = same_scope_count + diff_scope_count
    f1 = same_scope_count / total if total > 0 else 0.0

    return {
        "same_scope_consistent": same_scope_count,
        "diff_scope_inconsistent": diff_scope_count,
        "total_similar_pairs": total,
        "scope_f1_proxy": round(f1, 3),
        "scope_confusion_rate": round(diff_scope_count / total if total > 0 else 0.0, 3),
    }
