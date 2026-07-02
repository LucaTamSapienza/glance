"""Spike 0 — claim extraction via the OpenAI API (gpt-4o-mini).

Each snapshot -> atomic scoped claims with provenance (article, revid, timestamp,
snapshot_index 0=old/1=new) and a content hash over (body||scope).

Model change from claude-haiku-4-5-20251001 (original) to gpt-4o-mini due to
Anthropic API quota exhaustion. Prompt and output schema are unchanged.
Documented deviation: confirmatory run pre-registration, 2026-06-30."""
import os, json, hashlib, re, sys, time
from concurrent.futures import ThreadPoolExecutor, as_completed
from openai import OpenAI, RateLimitError, APIError

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "data")
import argparse
_args = argparse.ArgumentParser()
_args.add_argument("--model", default="gpt-4o-mini")
_args.add_argument("--out", default=None, help="output claims file (default: data/claims_<model>.json)")
_ARGS, _ = _args.parse_known_args()
MODEL = _ARGS.model
MAX_CLAIMS = 12
WORKERS = 5

SYSTEM = "You extract structured factual claims from text. Output ONLY valid JSON, no prose, no code fences."

USER_TMPL = '''From the text below extract up to {k} atomic, factual, scoped claims as a JSON array. Each element is an object:
{{"body": "<one self-contained atomic fact, no pronouns>", "scope": "<entity/context it is about>", "validity": "<time/condition it holds, or 'current'>"}}
Prefer claims with concrete values (numbers, dates, versions, statuses, names).
TEXT:
"""{text}"""'''

_client = None

def client():
    global _client
    if _client is None:
        _client = OpenAI()  # reads OPENAI_API_KEY from env
    return _client


def call_openai(text, _retries=4):
    prompt = USER_TMPL.format(k=MAX_CLAIMS, text=text[:4000])
    delay = 5
    for attempt in range(_retries):
        try:
            resp = client().chat.completions.create(
                model=MODEL,
                messages=[{"role": "system", "content": SYSTEM},
                          {"role": "user", "content": prompt}],
                temperature=0,
                max_tokens=1024,
            )
            return resp.choices[0].message.content or ""
        except RateLimitError:
            print(f"  rate-limited, retry in {delay}s", file=sys.stderr)
            time.sleep(delay)
            delay *= 2
        except APIError as e:
            print(f"  APIError: {e}, retry in {delay}s", file=sys.stderr)
            time.sleep(delay)
            delay *= 2
    return ""


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
    for _ in range(2):  # one retry on parse failure
        out = call_openai(snap["text"])
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

    # Output file is per-model so different extractor runs don't overwrite each other
    default_out = os.path.join(DATA, f"claims_{MODEL.replace('/', '_')}.json")
    out_path = _ARGS.out or default_out
    existing = []
    already_done = set()
    if os.path.exists(out_path):
        existing = json.load(open(out_path))
        # Only keep claims from this model run; don't mix extractors
        existing = [c for c in existing if c.get("extraction_model") == MODEL]
        snap_count = {}
        for c in existing:
            art = c["prov"]["article"]
            si = c["snapshot_index"]
            snap_count.setdefault(art, set()).add(si)
        already_done = {art for art, snaps in snap_count.items() if 0 in snaps and 1 in snaps}
        if already_done:
            print(f"Resuming: {len(existing)} gpt-4o-mini claims, {len(already_done)} articles complete")

    tasks = [(art["title"], si, snap)
             for art in corpus
             for si, snap in enumerate(art["snapshots"])
             if art["title"] not in already_done]
    if not tasks:
        print("All articles already extracted.")
        return
    print(f"{len(tasks)} snapshots from {len(corpus)-len(already_done)} articles remaining (workers={WORKERS})")

    new_results, done = [], 0
    with ThreadPoolExecutor(max_workers=WORKERS) as ex:
        futs = {ex.submit(extract_snapshot, t, si, snap): (t, si) for (t, si, snap) in tasks}
        for f in as_completed(futs):
            t, si = futs[f]
            try:
                cs = f.result()
            except Exception as e:
                cs = []
                print(f"  ERR {t}[{si}]: {e}", file=sys.stderr)
            # Tag with extraction model for resume logic
            for c in cs:
                c["extraction_model"] = MODEL
            new_results.extend(cs)
            done += 1
            print(f"  [{done:3d}/{len(tasks)}] {t[:40]:40s} snap{si} -> {len(cs)} claims", flush=True)
            # Checkpoint every 50 snapshots
            if done % 50 == 0:
                all_so_far = existing + new_results
                json.dump(all_so_far, open(out_path, "w"))
                print(f"  [checkpoint] {len(all_so_far)} claims saved", flush=True)

    all_results = existing + new_results
    json.dump(all_results, open(out_path, "w"))
    print(f"\nextracted {len(new_results)} new claims; {len(all_results)} total -> {out_path}")


if __name__ == "__main__":
    main()
