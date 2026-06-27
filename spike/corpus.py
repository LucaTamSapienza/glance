"""Spike 0 — corpus: a real Wikipedia domain subset with two real revision
snapshots per article (old -> current). The old->current pair is a genuine human
edit stream; emergent contradictions are whatever real editors changed (numbers,
dates, statuses). No synthetic edits here (injected contradictions are added
later, separately, and counted in their own Gate-3 column)."""
import os, re, json, time, html, sys
import requests

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "data")
API = "https://en.wikipedia.org/w/api.php"
CATEGORY = "Category:Web browsers"   # version/date facts churn -> real drift
OLD_BEFORE = "2021-01-01T00:00:00Z"  # ~5y old snapshot
N_ARTICLES = 60
MAXCHARS = 6000
S = requests.Session()
S.headers["User-Agent"] = "glance-spike0/0.1 (research; edoardo.simonettispallotta@gmail.com)"


def _get(params, _retries=5):
    params = {**params, "format": "json"}
    delay = 2.0
    for attempt in range(_retries):
        r = S.get(API, params=params, timeout=30)
        if r.status_code == 429:
            retry_after = int(r.headers.get("Retry-After", delay))
            print(f"  429 rate-limited, sleeping {retry_after}s ...", file=sys.stderr)
            time.sleep(retry_after)
            delay *= 2
            continue
        r.raise_for_status()
        return r.json()
    raise RuntimeError("Max retries exceeded")


def strip_html(h):
    h = re.sub(r"(?is)<(script|style|table|sup|ref)[^>]*>.*?</\1>", " ", h)
    h = re.sub(r"(?is)<[^>]+>", " ", h)
    h = html.unescape(h)
    h = re.sub(r"\[\d+\]", " ", h)          # [1] citation marks
    h = re.sub(r"\s+\n", "\n", h)
    h = re.sub(r"[ \t]{2,}", " ", h)
    return h.strip()


def member_titles():
    out, cont = [], {}
    while len(out) < N_ARTICLES:
        j = _get({"action": "query", "list": "categorymembers", "cmtitle": CATEGORY,
                  "cmtype": "page", "cmlimit": "100", **cont})
        out += [m["title"] for m in j["query"]["categorymembers"]]
        if "continue" not in j:
            break
        cont = j["continue"]
    return out[:N_ARTICLES]


def latest_revid(title):
    j = _get({"action": "query", "prop": "revisions", "titles": title,
              "rvprop": "ids|timestamp", "rvlimit": "1"})
    pg = next(iter(j["query"]["pages"].values()))
    rv = pg.get("revisions", [])
    return (rv[0]["revid"], rv[0]["timestamp"]) if rv else (None, None)


def old_revid(title):
    j = _get({"action": "query", "prop": "revisions", "titles": title,
              "rvprop": "ids|timestamp", "rvlimit": "1",
              "rvstart": OLD_BEFORE, "rvdir": "older"})
    pg = next(iter(j["query"]["pages"].values()))
    rv = pg.get("revisions", [])
    return (rv[0]["revid"], rv[0]["timestamp"]) if rv else (None, None)


def revision_text(revid):
    j = _get({"action": "parse", "oldid": revid, "prop": "text", "disabletoc": "1"})
    return strip_html(j["parse"]["text"]["*"])[:MAXCHARS]


def main():
    os.makedirs(DATA, exist_ok=True)
    titles = member_titles()
    print(f"{len(titles)} candidate titles from {CATEGORY}")
    corpus = []
    for i, t in enumerate(titles):
        try:
            new_id, new_ts = latest_revid(t)
            old_id, old_ts = old_revid(t)
            if not new_id or not old_id or old_id == new_id:
                continue
            snaps = [{"revid": old_id, "timestamp": old_ts, "text": revision_text(old_id)},
                     {"revid": new_id, "timestamp": new_ts, "text": revision_text(new_id)}]
            if min(len(s["text"]) for s in snaps) < 800:   # too short to stress extraction
                continue
            corpus.append({"title": t, "snapshots": snaps})
            print(f"  [{len(corpus):2d}] {t}  old={old_ts[:10]} new={new_ts[:10]} "
                  f"chars={len(snaps[0]['text'])}/{len(snaps[1]['text'])}")
            time.sleep(1.5)
        except Exception as e:
            print(f"  skip {t}: {e}", file=sys.stderr)
    out = os.path.join(DATA, "corpus.json")
    with open(out, "w") as f:
        json.dump(corpus, f)
    print(f"\nwrote {len(corpus)} articles x 2 snapshots -> {out}")


if __name__ == "__main__":
    main()
