"""Hot-node Gate 1 — TCP scope validation and propagation measurement.

Two-phase execution — the article list must be committed between phases:

  Phase 1:  python3 spike/corpus_hotnode.py --fetch
            Writes spike/data/hotnode_tcp_articles.json (titles + revids).
            STOP. Commit that file before running phase 2.

  Phase 2:  python3 spike/corpus_hotnode.py --extract
            Extracts claims, validates membership (criterion SHA-256 verified),
            runs oracle on validated cluster, reports raw k / validated k / rpe.

All thresholds and the membership criterion are pre-registered in:
  spike/artifacts/confirmatory_200art_20260630/hot_node_gate1_amendment.md
  spike/artifacts/confirmatory_200art_20260630/tcp_membership_criterion.md
"""
import argparse, hashlib, json, os, random, re, sys, time
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "data")
ARTIFACTS = os.path.join(HERE, "artifacts", "confirmatory_200art_20260630")
API_URL = "https://en.wikipedia.org/w/api.php"
OLD_BEFORE = "2021-01-01T00:00:00Z"
MAXCHARS = 6000

# ── Pre-registered membership criterion (SHA-256 locked) ──────────────────────
CRITERION_TEXT = (
    'A claim counts as scope=TCP iff its subject is the TCP protocol itself — its '
    'mechanisms, behavior, properties, or structure. A claim that merely mentions, '
    'contrasts with, or depends on TCP while being about another entity '
    '(e.g. "QUIC replaces TCP" → about QUIC; "HTTP/2 runs over TCP" → about HTTP/2) '
    'does not count.'
)
CRITERION_HASH = "8770d0c152b7b5273768727bac4f59511fd7862ffd7acaf938e3eca9c8a7bf9b"
# Note: the authoritative hash is in tcp_membership_criterion.md; verify below.


def _verify_criterion():
    actual = hashlib.sha256(CRITERION_TEXT.encode()).hexdigest()
    committed = "8770d0c152b7b5273768727bac4f59511fd7862ffd7acaf938e3eca9c8a7bf9b"
    if actual != committed:
        sys.exit(
            f"ABORT: membership criterion hash mismatch.\n"
            f"  embedded : {actual}\n"
            f"  committed: {committed}\n"
            "The criterion text has drifted from the pre-registered version."
        )
    print(f"  criterion hash verified: {committed[:16]}...")


# ── Wikipedia helpers ──────────────────────────────────────────────────────────
import requests, html as _html

_S = requests.Session()
_S.headers["User-Agent"] = "glance-spike0/hotnode (research; edoardo.simonettispallotta@gmail.com)"


def _get(params, retries=5):
    params = {**params, "format": "json"}
    delay = 2.0
    for _ in range(retries):
        r = _S.get(API_URL, params=params, timeout=30)
        if r.status_code == 429:
            wait = int(r.headers.get("Retry-After", delay))
            print(f"  429, sleeping {wait}s", file=sys.stderr)
            time.sleep(wait); delay *= 2; continue
        r.raise_for_status()
        return r.json()
    raise RuntimeError("Max retries exceeded")


def _category_members(cat, limit=200):
    out, cont = [], {}
    while True:
        j = _get({"action": "query", "list": "categorymembers", "cmtitle": cat,
                  "cmtype": "page", "cmlimit": "100", **cont})
        out += [m["title"] for m in j["query"]["categorymembers"]]
        if "continue" not in j or len(out) >= limit:
            break
        cont = j["continue"]
    return out[:limit]


def _title_search(query, limit=50):
    j = _get({"action": "query", "list": "search", "srsearch": query,
              "srnamespace": "0", "srlimit": str(limit)})
    return [r["title"] for r in j["query"]["search"]]


def _latest_revid(title):
    j = _get({"action": "query", "prop": "revisions", "titles": title,
              "rvprop": "ids|timestamp", "rvlimit": "1"})
    pg = next(iter(j["query"]["pages"].values()))
    rv = pg.get("revisions", [])
    return (rv[0]["revid"], rv[0]["timestamp"]) if rv else (None, None)


def _old_revid(title):
    j = _get({"action": "query", "prop": "revisions", "titles": title,
              "rvprop": "ids|timestamp", "rvlimit": "1",
              "rvstart": OLD_BEFORE, "rvdir": "older"})
    pg = next(iter(j["query"]["pages"].values()))
    rv = pg.get("revisions", [])
    return (rv[0]["revid"], rv[0]["timestamp"]) if rv else (None, None)


def _revision_text(revid):
    j = _get({"action": "parse", "oldid": revid, "prop": "text", "disabletoc": "1"})
    h = j["parse"]["text"]["*"]
    h = re.sub(r"(?is)<(script|style|table|sup|ref)[^>]*>.*?</\1>", " ", h)
    h = re.sub(r"(?is)<[^>]+>", " ", h)
    h = _html.unescape(h)
    h = re.sub(r"\[\d+\]", " ", h)
    h = re.sub(r"\s+", " ", h)
    return h.strip()[:MAXCHARS]


# ── Token-match rule (pre-registered) ─────────────────────────────────────────
_STOPWORDS = {"the", "a", "an", "of", "in", "for", "and", "on", "to", "by",
              "web", "browser", "browsers", "users", "list", "table", "protocol",
              "internet"}


def _scope_tokens(scope):
    words = re.sub(r"[^a-z0-9 ]", " ", scope.lower()).split()
    return frozenset(w for w in words if w and w not in _STOPWORDS)


def is_tcp_candidate(scope):
    """Token-match rule: contains 'tcp' OR (contains 'transmission' AND 'control')."""
    toks = _scope_tokens(scope)
    return "tcp" in toks or ("transmission" in toks and "control" in toks)


# ── Membership validation (gpt-4o-mini, blind to running k) ───────────────────
VALIDATION_SYSTEM = (
    "You apply a membership criterion to individual claims. "
    "Answer only YES or NO — no explanation."
)

VALIDATION_PROMPT = f"""Criterion (apply verbatim):
{CRITERION_TEXT}

Claim:
  scope: {{scope}}
  body: {{body}}

Does this claim count as scope=TCP under the criterion above?
Answer: YES or NO"""


def validate_claim(client, claim):
    """Returns True if gpt-4o-mini judges the claim as about TCP itself."""
    resp = client.chat.completions.create(
        model="gpt-4o-mini",
        messages=[
            {"role": "system", "content": VALIDATION_SYSTEM},
            {"role": "user", "content": VALIDATION_PROMPT.format(
                scope=claim["scope"], body=claim["body"])}
        ],
        temperature=0,
        max_tokens=5,
    )
    answer = resp.choices[0].message.content.strip().upper()
    return answer.startswith("YES")


def run_validation(raw_cluster, client):
    """Validate membership blind to running k — shuffled, one claim at a time."""
    shuffled = list(raw_cluster)
    random.shuffle(shuffled)  # no position signal about cumulative k

    accepted, rejected = [], []
    for i, c in enumerate(shuffled):
        verdict = validate_claim(client, c)
        if verdict:
            accepted.append(c)
        else:
            rejected.append(c)
        if (i + 1) % 10 == 0:
            print(f"  validated {i+1}/{len(shuffled)} ...", flush=True)

    return accepted, rejected


def spot_audit(raw_cluster, accepted_ids, client, frac=0.20, min_n=20):
    """Human-proxy spot-audit: re-validate a random sample, report agreement rate."""
    sample_size = max(min_n, int(len(raw_cluster) * frac))
    sample_size = min(sample_size, len(raw_cluster))
    sample = random.sample(raw_cluster, sample_size)

    agree = 0
    for c in sample:
        recheck = validate_claim(client, c)
        first_verdict = c["id"] in accepted_ids
        if recheck == first_verdict:
            agree += 1

    return sample_size, agree / sample_size if sample_size else 0.0


# ── Phase 1: fetch ─────────────────────────────────────────────────────────────
def phase_fetch(existing_titles):
    print("Phase 1: fetching TCP article candidates from Wikipedia ...")

    candidates = set()

    # Source 1: Category:Transmission_Control_Protocol
    cat_members = _category_members("Category:Transmission_Control_Protocol", limit=200)
    print(f"  {len(cat_members)} members from Category:Transmission_Control_Protocol")
    candidates.update(cat_members)

    # Source 2: title search "TCP"
    search_hits = _title_search("TCP protocol", limit=50)
    print(f"  {len(search_hits)} hits from title search 'TCP protocol'")
    candidates.update(search_hits)

    # Exclude articles already in the 200-article corpus
    candidates = [t for t in candidates if t not in existing_titles]
    print(f"  {len(candidates)} candidates after excluding existing corpus")

    # Qualify: two real snapshots, old ≤ 2021-01-01, both ≥ 800 chars, old ≠ new
    articles = []
    for title in candidates:
        if len(articles) >= 40:
            break
        try:
            new_id, new_ts = _latest_revid(title)
            old_id, old_ts = _old_revid(title)
            if not new_id or not old_id or old_id == new_id:
                continue
            old_text = _revision_text(old_id)
            new_text = _revision_text(new_id)
            if min(len(old_text), len(new_text)) < 800:
                continue
            articles.append({
                "title": title,
                "snapshots": [
                    {"revid": old_id, "timestamp": old_ts, "text": old_text},
                    {"revid": new_id, "timestamp": new_ts, "text": new_text},
                ]
            })
            print(f"  [{len(articles):2d}] {title}  old={old_ts[:10]} new={new_ts[:10]}")
            time.sleep(1.0)
        except Exception as e:
            print(f"  skip {title}: {e}", file=sys.stderr)

    out = os.path.join(DATA, "hotnode_tcp_articles.json")
    with open(out, "w") as f:
        json.dump(articles, f, indent=2)
    print(f"\nWrote {len(articles)} articles -> {out}")
    print("\nNEXT: commit spike/data/hotnode_tcp_articles.json, then run --extract")


# ── Phase 2: extract + validate + measure ─────────────────────────────────────
def phase_extract():
    import sys as _sys
    _sys.path.insert(0, HERE)
    from openai import OpenAI
    from oracle import Oracle

    _verify_criterion()

    client = OpenAI()
    oracle = Oracle()

    # Load existing 200-art claims
    existing_claims = json.load(open(os.path.join(DATA, "claims.json")))
    print(f"  {len(existing_claims)} claims from 200-art corpus")

    # Load new TCP articles
    tcp_articles_path = os.path.join(DATA, "hotnode_tcp_articles.json")
    if not os.path.exists(tcp_articles_path):
        sys.exit("hotnode_tcp_articles.json not found — run --fetch first and commit")
    tcp_articles = json.load(open(tcp_articles_path))
    print(f"  {len(tcp_articles)} TCP articles to extract")

    # Extract claims from new TCP articles (same prompt/model as confirmatory run)
    from extract import extract_snapshot
    new_claims = []
    total = sum(len(a["snapshots"]) for a in tcp_articles)
    done = 0
    for art in tcp_articles:
        for si, snap in enumerate(art["snapshots"]):
            cs = extract_snapshot(art["title"], si, snap)
            for c in cs:
                c["extraction_model"] = "gpt-4o-mini"
            new_claims.extend(cs)
            done += 1
            print(f"  [{done:3d}/{total}] {art['title'][:40]:40s} snap{si} -> {len(cs)} claims",
                  flush=True)

    all_claims = existing_claims + new_claims
    print(f"\n  Total claims: {len(all_claims)} ({len(new_claims)} new from TCP articles)")

    # ── Raw candidate cluster (token-match) ───────────────────────────────────
    raw_cluster = [c for c in all_claims
                   if c.get("snapshot_index") == 1 and is_tcp_candidate(c["scope"])]
    raw_k = len(raw_cluster)
    print(f"\n  Raw token-match cluster: k={raw_k}")

    if raw_k == 0:
        print("  No claims matched TCP token rule. UNRESOLVABLE.")
        return

    # ── Membership validation (gpt-4o-mini, shuffled, blind to k) ────────────
    print(f"\n  Running membership validation (gpt-4o-mini, shuffled, criterion SHA-256 locked) ...")
    accepted, rejected = run_validation(raw_cluster, client)
    validated_k = len(accepted)
    drop_rate = (raw_k - validated_k) / raw_k

    accepted_ids = {c["id"] for c in accepted}

    print(f"\n  raw_k={raw_k}  validated_k={validated_k}  drop_rate={drop_rate:.1%}")

    # Rejection breakdown
    print(f"  rejected={len(rejected)} claims (not about TCP itself by criterion)")
    if drop_rate > 0.50:
        print("  WARNING: drop_rate >50% — token-match surface substantially contaminated; "
              "report this as a finding, not a cleanup step")

    # ── Spot audit ────────────────────────────────────────────────────────────
    print(f"\n  Running spot audit ...")
    audit_n, audit_agree = spot_audit(raw_cluster, accepted_ids, client)
    print(f"  spot_audit: n={audit_n}  agreement={audit_agree:.1%}")
    if audit_agree < 0.90:
        print("  WARNING: spot audit agreement <90% — automated validation unreliable; "
              "results flagged")

    # ── Hot-node threshold check ───────────────────────────────────────────────
    if validated_k < 25:
        print(f"\n  UNRESOLVABLE: validated_k={validated_k} < 25 threshold.")
        print("  Wikipedia-shaped knowledge does not concentrate enough to test hot-node "
              "propagation on TCP. This is NOT a Gate 1 PASS.")
        print("  The real test requires a concentrating corpus (agent memory / codebase / "
              "engineering wiki).")
        return

    print(f"\n  validated_k={validated_k} >= 25 — proceeding to propagation measurement")

    # ── Oracle measurement on validated cluster ────────────────────────────────
    n = validated_k
    contradictions = 0
    pairs_checked = 0
    for i in range(n):
        for j in range(i + 1, n):
            if oracle.contradicts(accepted[i]["body"], accepted[j]["body"]):
                contradictions += 1
            pairs_checked += 1

    rpe = contradictions / max(n - 1, 1)
    print(f"\n  validated_k={n}  oracle_contradictions={contradictions}  "
          f"pairs_checked={pairs_checked}  rpe={rpe:.3f}")

    # ── Verdict (pre-registered thresholds from hot_node_gate1_amendment.md) ──
    print(f"\n  Pre-registered thresholds: PASS if p95_rpe<=2; KILL if p95_rpe>10")
    # With one node, p95 rpe == rpe (singleton distribution)
    p95_rpe = rpe
    print(f"  p95_rpe={p95_rpe:.3f}")

    if p95_rpe <= 2.0:
        verdict = "PASS"
        note = "propagation bounded; hot-node merges are cheap"
    elif p95_rpe > 10.0:
        verdict = "KILL"
        note = "hot-node merges are prohibitively expensive"
    else:
        verdict = "MARGINAL"
        note = "bounded but non-negligible; discuss in §7 scope"

    print(f"\nGATE 1 (hot-node): {verdict}  ({note})")
    print(f"\n  Summary:")
    print(f"    raw_k={raw_k}  validated_k={validated_k}  drop_rate={drop_rate:.1%}")
    print(f"    oracle_contradictions={contradictions}  rpe={rpe:.3f}  p95_rpe={p95_rpe:.3f}")
    print(f"    spot_audit: n={audit_n}  agreement={audit_agree:.1%}")


# ── Entry point ────────────────────────────────────────────────────────────────
def main():
    p = argparse.ArgumentParser()
    p.add_argument("--fetch", action="store_true",
                   help="Phase 1: fetch TCP articles and write article list")
    p.add_argument("--extract", action="store_true",
                   help="Phase 2: extract, validate, and measure propagation")
    args = p.parse_args()

    if not args.fetch and not args.extract:
        p.print_help()
        sys.exit(1)
    if args.fetch and args.extract:
        sys.exit("Run --fetch and --extract as separate steps; commit the article list between them.")

    os.makedirs(DATA, exist_ok=True)

    if args.fetch:
        # Load existing corpus titles so we don't re-fetch them
        corpus_path = os.path.join(DATA, "corpus.json")
        existing_titles = set()
        if os.path.exists(corpus_path):
            existing_titles = {a["title"] for a in json.load(open(corpus_path))}
        phase_fetch(existing_titles)

    elif args.extract:
        phase_extract()


if __name__ == "__main__":
    main()
