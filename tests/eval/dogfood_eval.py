#!/usr/bin/env python3
"""Dogfood evaluation: glance as the retrieval/write layer for daily agent tasks.

Compares three ways an agent can answer questions over the memory/ vault:
  baseline        no glance: grep for query terms, read every matching file whole
  glance          ./glance --context Q DIR --budget N        (BM25 + link graph)
  glance_semantic same, + --semantic (MiniLM, needs the GLANCE_SEMANTIC build)

and two ways to apply updates:
  baseline        rewrite the whole file (tokens: read + write the full file)
  glance          ./glance --edit / --set-frontmatter        (surgical, atomic)

Multi-hop questions additionally compare GLANCE_GRAPH_KHOP=0 vs 2.
All edits run on a throwaway copy of the vault. Results: test_results.jsonl.
"""
import json, os, re, shutil, subprocess, sys, time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GLANCE = os.path.join(REPO, "glance")
VAULT = os.path.join(REPO, "memory")
SCRATCH = sys.argv[1] if len(sys.argv) > 1 else "/tmp"
COPY = os.path.join(SCRATCH, "vault_copy")
OUT = os.environ.get("EVAL_OUT", os.path.join(REPO, "test_results.jsonl"))
BUDGET = os.environ.get("EVAL_BUDGET", "4000")
RUN = os.environ.get("RUN_LABEL", "run")

STOP = set("""a an the and or of to in on for with how do does is are was what which
why when where can i my me it its this that still there no not left get""".split())


def est_tokens(text):
    """Mirror receipt.c's heuristic: max(bytes/4, words)."""
    b = len(text.encode("utf-8", "replace"))
    return max(b // 4, len(text.split()))


def run(cmd, env=None, timeout=120):
    """Run a command synchronously; return (exit, stdout, stderr, ms)."""
    e = dict(os.environ)
    if env:
        e.update(env)
    t0 = time.monotonic()
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, env=e)
    ms = round((time.monotonic() - t0) * 1000, 1)
    return p.returncode, p.stdout, p.stderr, ms


def accuracy(phrases, text):
    """Fraction of expected phrases present (case-insensitive substring)."""
    if not phrases:
        return None
    low = text.lower()
    return round(sum(1 for p in phrases if p.lower() in low) / len(phrases), 2)


def baseline_read(vault, query):
    """No-glance simulation: grep query terms, read matching files whole."""
    terms = [w for w in re.findall(r"[a-zA-Z0-9#]+", query.lower())
             if len(w) > 2 and w not in STOP]
    t0 = time.monotonic()
    files = sorted(f for f in os.listdir(vault) if f.endswith(".md"))
    hit, text = [], ""
    for f in files:
        body = open(os.path.join(vault, f), encoding="utf-8").read()
        if any(t in body.lower() for t in terms):
            hit.append(f)
            text += body
    if not hit:                      # nothing matched: an agent reads everything
        for f in files:
            text += open(os.path.join(vault, f), encoding="utf-8").read()
        hit = files
    ms = round((time.monotonic() - t0) * 1000, 1)
    return {"tokens": est_tokens(text), "files_read": hit, "latency_ms": ms}, text


def glance_context(vault, query, semantic, env=None):
    """One --context run; returns (mode-record, bundle-text)."""
    cmd = [GLANCE, "--context", query, vault, "--budget", BUDGET]
    if semantic:
        cmd.append("--semantic")
    code, out, err, ms = run(cmd, env=env)
    rec = {"exit": code, "latency_ms": ms}
    text = ""
    try:
        d = json.loads(out)
        text = "".join(c.get("text", "") for c in d.get("chunks", []))
        rec.update(tokens=d["receipt"]["used_tokens"],
                   raw_tokens=d["receipt"]["raw_tokens"],
                   saved_pct=d["receipt"]["saved_pct"],
                   chunks=len(d.get("chunks", [])),
                   top_score=round(max((c.get("score", 0.0)
                                        for c in d.get("chunks", [])), default=0.0), 2),
                   sources=sorted({c["note"] for c in d.get("chunks", [])}),
                   json_valid=True)
    except (json.JSONDecodeError, KeyError):
        rec.update(json_valid=False, tokens=est_tokens(out), error=err[:200])
    return rec, text


QUESTIONS = [
    # ten simulated daily questions
    ("Q1", "Why does make test sometimes fall back to UBSan only?", False,
     ["shadow", "dyld shared cache", "probe"]),
    ("Q2", "What is the default keyboard mode and why?", False,
     ["legacy", "kitty", "leak"]),
    ("Q3", "How do I un-wedge a terminal left printing CSI-u key reports?", True,
     ["x1b[<10u", "kitty", "pop"]),
    ("Q4", "What happened to the original PR #10 and how do we avoid it again?", True,
     ["retarget", "base", "delete"]),
    ("Q5", "Which embedding model will replace MiniLM and why?", False,
     ["embeddinggemma", "english-only", "256"]),
    ("Q6", "What is still open on the agent side?", False,
     ["ascii", "receipt", "heuristic"]),
    ("Q7", "Why is there no AI chat inside the editor?", False,
     ["ai-edit", "reverted", "agent layer"]),
    ("Q8", "What was the verdict on Eddie's claim store?", True,
     ["no-go", "gate 1", "hot-node"]),
    ("Q9", "How can the TUI be tested without a real terminal?", False,
     ["pty", "tiocswinsz", "sigwinch"]),
    ("Q10", "When was the memory vault created and what did it replace?", True,
     ["2026-07-01", "status.md", "context.md"]),
    # two of my own: an unanswerable query (sparse-return check) + an Italian one
    ("Q11", "How do I remap vim keybindings in the glance config file?", False, []),
    ("Q12", "Quale modello di embedding sostituira MiniLM e perche?", False,
     ["embeddinggemma"]),
]

# ops: (id, argv-after-glance, expected substring, placement check)
EDITS = [
    ("U1", ["--edit", "status.md", "append", "Open",
            "- eval: dogfood suite added a synthetic open item"],
     "synthetic open item", "in_section:Open"),
    ("U2", ["--edit", "decisions.md", "before",
            "2026-07-11 — Doctor's contract set by dogfood: measure meaning, gate on clean",
            "## 2026-07-21 — Dogfood eval: retrieval measured against a no-glance baseline\n\nSynthetic entry written by the eval suite."],
     "Dogfood eval: retrieval measured", "above:2026-07-11 — Doctor's contract"),
    ("U3", ["--edit", "history.md", "append", "2026-07-11 — The dogfood pass",
            "Eval-suite line: appended without touching the rest of the note."],
     "Eval-suite line", "in_section:2026-07-11 — The dogfood pass"),
    ("U4", ["--set-frontmatter", "status.md", "reviewed", "2026-07-21"],
     "reviewed: 2026-07-21", "frontmatter"),
    ("U5", ["--edit", "lessons.md", "insert", "The macOS pasteboard is lazy",
            "Eval note: inserted at the top of the section."],
     "inserted at the top", "in_section:The macOS pasteboard is lazy"),
    ("U6", ["--edit", "MEMORY.md", "append", "Protocol (distill, don't accumulate)",
            "Eval-suite protocol line (heading with parentheses)."],
     "heading with parentheses", "in_section:Protocol"),
    ("U7", ["--edit", "status.md", "replace", "Open",
            "Replaced by the eval suite to measure a whole-section rewrite."],
     "Replaced by the eval suite", "in_section:Open"),
    ("U8", ["--edit", "lessons.md", "append", "NCBLIT_PIXEL needs an aspect-tight plane",
            "Eval line under an all-caps technical heading."],
     "all-caps technical heading", "in_section:NCBLIT_PIXEL"),
    ("U9", ["--edit", "status.md", "append", "Nonexistent Heading",
            "must never land"], None, "expect_failure"),
    ("U10", ["--edit", "decisions.md", "append",
             "2026-06-25 — Paper direction: the contribution is accuracy-per-token",
             "Nota eval con unicode: città, però, ✓ — sopravvive?"],
     "città, però, ✓", "in_section:2026-06-25 — Paper direction"),
]


def section_bounds(lines, head_frag):
    """(start, end) line indexes of the ## section whose heading contains frag."""
    s = next(i for i, l in enumerate(lines)
             if l.startswith("##") and head_frag.lower() in l.lower())
    e = next((i for i in range(s + 1, len(lines)) if lines[i].startswith("## ")),
             len(lines))
    return s, e


def check_placement(path, expect, rule):
    lines = open(path, encoding="utf-8").read().splitlines()
    if rule == "frontmatter":
        return lines[0] == "---" and any(expect in l for l in lines[:6])
    kind, _, frag = rule.partition(":")
    s, e = section_bounds(lines, frag)
    if kind == "in_section":
        return any(expect in l for l in lines[s:e])
    if kind == "above":       # expect-text must sit before the named heading
        return any(expect in l for l in lines[:s])
    return False


def main():
    results = []

    # --- fresh working copy of the vault ------------------------------------
    if os.path.exists(COPY):
        shutil.rmtree(COPY)
    shutil.copytree(VAULT, COPY, ignore=shutil.ignore_patterns(".glance"))
    os.makedirs(os.path.join(COPY, ".obsidian"), exist_ok=True)

    # warm the semantic cache once so per-question latencies are steady-state
    _, _, _, cold_ms = run([GLANCE, "--context", "warmup", COPY,
                            "--budget", "200", "--semantic"])

    # --- questions -----------------------------------------------------------
    for qid, query, multihop, expected in QUESTIONS:
        base, base_text = baseline_read(COPY, query)
        base["accuracy"] = accuracy(expected, base_text)
        lex, lex_text = glance_context(COPY, query, semantic=False)
        lex["accuracy"] = accuracy(expected, lex_text)
        sem, sem_text = glance_context(COPY, query, semantic=True)
        sem["accuracy"] = accuracy(expected, sem_text)

        rec = {"kind": "question", "id": qid, "query": query,
               "multi_hop": multihop, "expected_phrases": expected,
               "modes": {"baseline": base, "glance": lex, "glance_semantic": sem}}
        if multihop:  # does graph expansion change what the bundle contains?
            h0, t0 = glance_context(COPY, query, True, {"GLANCE_GRAPH_KHOP": "0"})
            h2, t2 = glance_context(COPY, query, True, {"GLANCE_GRAPH_KHOP": "2"})
            rec["khop_probe"] = {
                "khop0": {"accuracy": accuracy(expected, t0),
                          "tokens": h0.get("tokens"), "sources": h0.get("sources")},
                "khop2": {"accuracy": accuracy(expected, t2),
                          "tokens": h2.get("tokens"), "sources": h2.get("sources")}}
        best = max(lex.get("tokens", 0), 1)
        rec["delta"] = {
            "tokens_glance_vs_baseline_pct":
                round(100 * (1 - lex.get("tokens", 0) / max(base["tokens"], 1))),
            "tokens_semantic_vs_baseline_pct":
                round(100 * (1 - sem.get("tokens", 0) / max(base["tokens"], 1))),
            "accuracy_glance_minus_baseline":
                None if base["accuracy"] is None
                else round((lex["accuracy"] or 0) - base["accuracy"], 2)}
        results.append(rec)

    # --- updates -------------------------------------------------------------
    for uid, args, expect, rule in EDITS:
        path = os.path.join(COPY, args[1])
        before = open(path, encoding="utf-8").read()
        argv = [GLANCE] + args[:1] + [path] + args[2:]
        code, out, err, ms = run(argv)
        after = open(path, encoding="utf-8").read()
        cmd_tokens = est_tokens(" ".join(args)) + est_tokens(out)
        rec = {"kind": "update", "id": uid, "op": args[0] + ":" + args[2] if args[0] == "--edit" else args[0],
               "file": args[1],
               "modes": {
                   "baseline": {"tokens": 2 * est_tokens(before),
                                "note": "read + rewrite the whole file"},
                   "glance": {"tokens": cmd_tokens, "exit": code,
                              "latency_ms": ms}}}
        if rule == "expect_failure":
            ok = code != 0 and after == before
            rec["modes"]["glance"]["failed_cleanly"] = ok
            rec["modes"]["glance"]["file_untouched"] = after == before
        else:
            placed = code == 0 and check_placement(path, expect, rule)
            rec["modes"]["glance"]["placed_correctly"] = placed
            rec["modes"]["glance"]["rest_of_file_intact"] = (
                sum(1 for a, b in zip(before.splitlines(), after.splitlines())
                    if a != b) <= len(after.splitlines()))
        rec["delta"] = {"tokens_glance_vs_baseline_pct": round(
            100 * (1 - rec["modes"]["glance"]["tokens"]
                   / max(rec["modes"]["baseline"]["tokens"], 1)))}
        results.append(rec)

    # vault still lints clean after all ten edits?
    code, out, _, _ = run([GLANCE, "--doctor", COPY])
    try:
        doctor_clean = json.loads(out)["summary"]["clean"]
    except Exception:
        doctor_clean = None

    # --- summary -------------------------------------------------------------
    qs = [r for r in results if r["kind"] == "question"]
    us = [r for r in results if r["kind"] == "update"]
    scored = [r for r in qs if r["expected_phrases"]]

    def mean(xs):
        xs = [x for x in xs if x is not None]
        return round(sum(xs) / len(xs), 2) if xs else None

    q11 = next((r for r in qs if r["id"] == "Q11"), None)
    summary = {"kind": "summary", "date": "2026-07-22",
               "vault": "memory/ (copy)", "budget_tokens": int(BUDGET),
               "top_score_answered_mean":
                   mean([r["modes"]["glance"].get("top_score") for r in scored]),
               "top_score_no_answer_q11":
                   q11 and q11["modes"]["glance"].get("top_score"),
               "semantic_cold_start_ms": cold_ms,
               "questions": {
                   "count": len(qs),
                   "mean_tokens": {m: mean([r["modes"][m].get("tokens") for r in qs])
                                   for m in ("baseline", "glance", "glance_semantic")},
                   "mean_accuracy": {m: mean([r["modes"][m]["accuracy"] for r in scored])
                                     for m in ("baseline", "glance", "glance_semantic")},
                   "mean_latency_ms": {m: mean([r["modes"][m]["latency_ms"] for r in qs])
                                       for m in ("baseline", "glance", "glance_semantic")},
                   "mean_tokens_saved_vs_baseline_pct":
                       mean([r["delta"]["tokens_glance_vs_baseline_pct"] for r in qs])},
               "updates": {
                   "count": len(us),
                   "placed_correctly": sum(1 for r in us
                                           if r["modes"]["glance"].get("placed_correctly")),
                   "clean_failures": sum(1 for r in us
                                         if r["modes"]["glance"].get("failed_cleanly")),
                   "mean_tokens": {"baseline": mean([r["modes"]["baseline"]["tokens"] for r in us]),
                                   "glance": mean([r["modes"]["glance"]["tokens"] for r in us])},
                   "mean_tokens_saved_vs_baseline_pct":
                       mean([r["delta"]["tokens_glance_vs_baseline_pct"] for r in us]),
                   "doctor_clean_after_all_edits": doctor_clean}}
    results.append(summary)

    with open(OUT, "w", encoding="utf-8") as f:
        for r in results:
            f.write(json.dumps({"run": RUN, **r}, ensure_ascii=False) + "\n")
    print(f"wrote {len(results)} records -> {OUT}")
    print(json.dumps(summary, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
