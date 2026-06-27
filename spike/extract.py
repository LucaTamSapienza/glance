"""Spike 0 — claim extraction via the Claude Code CLI (claude -p).

Each snapshot -> atomic scoped claims with provenance (article, revid, timestamp,
snapshot_index 0=old/1=new) and a content hash over (body||scope). Concurrency
hides the ~13s/call latency. Run from /tmp so each call doesn't reload project
context."""
import os, json, subprocess, hashlib, re, sys, tempfile
from concurrent.futures import ThreadPoolExecutor, as_completed

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "data")
MODEL = "claude-haiku-4-5-20251001"
MAX_CLAIMS = 12
WORKERS = 5
TMP = tempfile.gettempdir()

PROMPT = '''Output ONLY a JSON array (no prose, no code fences). From the text below extract up to {k} atomic, factual, scoped claims. Each is an object:
{{"body": "<one self-contained atomic fact, no pronouns>", "scope": "<entity/context it is about>", "validity": "<time/condition it holds, or 'current'>"}}
Prefer claims with concrete values (numbers, dates, versions, statuses, names).
TEXT:
"""{text}"""'''


def call_claude(prompt):
    r = subprocess.run(["claude", "-p", "--model", MODEL, prompt],
                       capture_output=True, text=True, timeout=180, cwd=TMP)
    return r.stdout


def parse_json_array(s):
    s = s.strip()
    s = re.sub(r"^```(?:json)?", "", s).strip()
    s = re.sub(r"```$", "", s).strip()
    m = re.search(r"\[.*\]", s, re.S)
    if m:
        s = m.group(0)
    return json.loads(s)


def chash(body, scope):
    norm = re.sub(r"\s+", " ", f"{body}||{scope}".lower()).strip()
    return hashlib.sha256(norm.encode()).hexdigest()[:16]


def extract_snapshot(title, si, snap):
    out = ""
    for _ in range(2):  # one retry on parse failure
        out = call_claude(PROMPT.format(k=MAX_CLAIMS, text=snap["text"][:4000]))
        try:
            arr = parse_json_array(out)
            break
        except Exception:
            arr = None
    if not arr:
        return []
    claims = []
    for c in arr:
        if not isinstance(c, dict):
            continue
        body = str(c.get("body", "")).strip()
        scope = str(c.get("scope", "")).strip()
        if not body:
            continue
        claims.append({
            "id": chash(body, scope), "body": body, "scope": scope,
            "validity": str(c.get("validity", "current")).strip(),
            "snapshot_index": si,
            "prov": {"article": title, "revid": snap["revid"], "timestamp": snap["timestamp"]},
        })
    return claims


def main():
    corpus = json.load(open(os.path.join(DATA, "corpus.json")))
    tasks = [(art["title"], si, snap) for art in corpus for si, snap in enumerate(art["snapshots"])]
    print(f"{len(tasks)} snapshots from {len(corpus)} articles -> extracting (workers={WORKERS})")
    results, done = [], 0
    with ThreadPoolExecutor(max_workers=WORKERS) as ex:
        futs = {ex.submit(extract_snapshot, t, si, snap): (t, si) for (t, si, snap) in tasks}
        for f in as_completed(futs):
            t, si = futs[f]
            try:
                cs = f.result()
            except Exception as e:
                cs = []
                print(f"  ERR {t}[{si}]: {e}", file=sys.stderr)
            results.extend(cs)
            done += 1
            print(f"  [{done:3d}/{len(tasks)}] {t[:40]:40s} snap{si} -> {len(cs)} claims")
    out = os.path.join(DATA, "claims.json")
    json.dump(results, open(out, "w"))
    print(f"\nextracted {len(results)} claims from {len(corpus)} articles -> {out}")


if __name__ == "__main__":
    main()
