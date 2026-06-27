"""Spike 0 — independent contradiction oracle (built FIRST, then FROZEN).

Method is NLI (entailment/neutral/contradiction over a claim pair), deliberately
distinct from the canonicalizer's entity/scope-matching logic, so Gate 3 is not
graded against a copy of itself.

Run `python oracle.py` once to: build a transparent rule-labeled validation set
(verifiable ground truth, not human labels — documented as such), sweep the
threshold, record precision/recall, and write a read-only frozen artifact under
spike/artifacts/. After freezing, the threshold is NEVER retuned (Guard 1).
"""
import os, json, time, random, stat, functools
import torch
from transformers import AutoTokenizer, AutoModelForSequenceClassification

MODEL = "MoritzLaurer/DeBERTa-v3-base-mnli-fever-anli"  # labels: entail/neutral/contra
HERE = os.path.dirname(os.path.abspath(__file__))
ART = os.path.join(HERE, "artifacts")
FROZEN = os.path.join(HERE, "artifacts", "oracle_frozen.json")  # pointer to the frozen run


class Oracle:
    """NLI contradiction detector. `contradicts(a,b)` is symmetric (max over both
    directions). Threshold is loaded from the frozen artifact unless overridden
    during the one-time build/validation."""

    def __init__(self, threshold=None, model=MODEL, device=None):
        self.device = device or ("mps" if torch.backends.mps.is_available() else "cpu")
        self.tok = AutoTokenizer.from_pretrained(model)
        self.model = AutoModelForSequenceClassification.from_pretrained(model).to(self.device).eval()
        self.contra_idx = next(i for i, l in self.model.config.id2label.items()
                               if "contradiction" in l.lower())
        if threshold is None:
            with open(FROZEN) as f:
                threshold = json.load(f)["threshold"]
        self.threshold = float(threshold)

    @torch.no_grad()
    def _contra_prob_directed(self, premise, hypothesis):
        x = self.tok(premise, hypothesis, return_tensors="pt", truncation=True, max_length=256).to(self.device)
        return float(self.model(**x).logits.softmax(-1)[0][self.contra_idx])

    @functools.lru_cache(maxsize=200_000)
    def _score(self, a, b):
        return max(self._contra_prob_directed(a, b), self._contra_prob_directed(b, a))

    def score(self, a, b):
        return self._score(a, b)

    def contradicts(self, a, b):
        s = self._score(a, b)
        return (s >= self.threshold, s)


# ---- transparent rule-labeled validation set (verifiable ground truth) ----
# NOTE: these are RULE-CONSTRUCTED labels, not human labels. They measure the
# instrument's intrinsic contradiction-detection P/R; domain + human validation
# is the proper upgrade before any publication claim. Documented in the artifact.

def build_validation_set(seed=0):
    rng = random.Random(seed)
    subjects = ["The Eiffel Tower", "Python 3.12", "The library", "Mount Everest",
                "The company", "The treaty", "The river", "The algorithm",
                "The novel", "The bridge", "The vaccine", "The processor"]
    pos, neg = [], []  # (a, b, label) label 1 = contradiction

    # 1) mutually-exclusive value swaps -> contradiction
    val_pairs = [("330 metres tall", "450 metres tall"), ("released in 2023", "released in 2021"),
                 ("located in France", "located in Italy"), ("the largest in Europe", "the smallest in Europe"),
                 ("written in C", "written in Rust"), ("founded in 1990", "founded in 2005"),
                 ("approved by the FDA", "rejected by the FDA"), ("open source", "proprietary")]
    for s in subjects:
        a, b = rng.choice(val_pairs)
        pos.append((f"{s} is {a}.", f"{s} is {b}.", 1))

    # 2) negation -> contradiction
    preds = ["supports concurrency", "is still maintained", "passed the vote",
             "increased revenue", "contains mercury", "was completed on time"]
    for s in subjects:
        p = rng.choice(preds)
        pos.append((f"{s} {p}.", f"{s} does not {p.replace('is ','').replace('was ','')}.".replace("does not is","is not").replace("does not was","was not"), 1))

    # 3) antonym direction -> contradiction
    anto = [("increased", "decreased"), ("expanded", "contracted"), ("rose", "fell"),
            ("improved", "worsened"), ("opened", "closed")]
    metrics = ["the population", "the temperature", "the deficit", "the membership", "the price"]
    for m in metrics:
        a, b = rng.choice(anto)
        pos.append((f"In 2020 {m} {a}.", f"In 2020 {m} {b}.", 1))

    # 4) paraphrase -> NOT contradiction
    para = [("The Eiffel Tower is 330 metres tall.", "Standing 330 metres, the Eiffel Tower is that tall."),
            ("Python 3.12 was released in 2023.", "The 2023 release was Python 3.12."),
            ("The river flows north.", "Northward is the direction the river flows."),
            ("The vaccine is FDA-approved.", "The FDA has approved the vaccine."),
            ("The bridge spans 2 km.", "The bridge has a span of two kilometres.")]
    for a, b in para * 3:
        neg.append((a, b, 0))

    # 5) entailment / hypernym -> NOT contradiction
    entail = [("A poodle is a dog.", "A poodle is an animal."),
              ("The novel won the Booker Prize.", "The novel won a literary award."),
              ("The processor has 8 cores.", "The processor is multi-core."),
              ("Everest is 8,849 m high.", "Everest is over 8,000 m high.")]
    for a, b in entail * 4:
        neg.append((a, b, 0))

    # 6) topically-related but COMPATIBLE (hard negatives) -> NOT contradiction
    for s in subjects:
        neg.append((f"{s} was founded in 1990.", f"{s} is headquartered in Berlin.", 0))
        neg.append((f"{s} is open source.", f"{s} is written in C.", 0))

    # 7) unrelated factual pairs -> NOT contradiction
    unrel = [("The Eiffel Tower is in Paris.", "Python is a programming language."),
             ("Mount Everest is in the Himalayas.", "The treaty was signed in 1648."),
             ("The vaccine prevents measles.", "The bridge carries a railway.")]
    for a, b in unrel * 4:
        neg.append((a, b, 0))

    # 8) HARD scope-distinct negatives -> NOT contradiction (different scope/time/entity).
    #    These directly probe the scope-confusion false-positive mode that would poison Gate 3.
    scope_neg = [
        ("Project Alpha's deadline is Friday.", "Project Beta's deadline is Monday."),
        ("Python 2's print is a statement.", "Python 3's print is a function."),
        ("In 2019 revenue rose.", "In 2021 revenue fell."),
        ("The London office has 50 staff.", "The Berlin office has 80 staff."),
        ("Version 1 of the API is deprecated.", "Version 3 of the API is supported."),
        ("The 2018 model had 4 cores.", "The 2022 model has 8 cores."),
        ("Firefox 50 was released in 2016.", "Firefox 120 was released in 2023."),
        ("The northern branch closed.", "The southern branch opened."),
        ("Under the old policy, fees applied.", "Under the new policy, fees are waived."),
        ("The beta supports Linux.", "The stable release supports Windows."),
        ("The 2010 census counted 5 million.", "The 2020 census counted 7 million."),
        ("Chapter 1 is set in winter.", "Chapter 2 is set in summer."),
    ]
    for a, b in scope_neg:
        neg.append((a, b, 0))

    # 9) SUBTLE same-scope contradictions -> contradiction (numeric drift / status flip)
    subtle_pos = [
        ("As of the latest release, the browser supports Manifest V2 extensions.",
         "The latest release removed support for Manifest V2 extensions."),
        ("The company reported a profit in fiscal 2023.", "The company reported a loss in fiscal 2023."),
        ("Everest's officially recognized height is 8,848 m.", "Everest's officially recognized height is 8,849 m."),
        ("The treaty entered into force in 1994.", "The treaty never entered into force."),
        ("The browser's current engine is Gecko.", "The browser's current engine is Blink."),
        ("The latest stable version is 4.2.", "The latest stable version is 5.0."),
        ("Support for the format was added in this release.", "Support for the format was dropped in this release."),
        ("The current CEO is Jane Doe.", "The current CEO is John Smith."),
    ]
    for a, b in subtle_pos:
        pos.append((a, b, 1))

    data = pos + neg
    rng.shuffle(data)
    return data


def sweep_and_freeze():
    os.makedirs(ART, exist_ok=True)
    data = build_validation_set()
    orc = Oracle(threshold=0.5)  # temp; we sweep below
    scores = [(orc.score(a, b), lab) for a, b, lab in data]
    best = None
    for t in [i / 100 for i in range(30, 96, 5)]:
        tp = sum(1 for s, l in scores if s >= t and l == 1)
        fp = sum(1 for s, l in scores if s >= t and l == 0)
        fn = sum(1 for s, l in scores if s < t and l == 1)
        prec = tp / (tp + fp) if tp + fp else 0.0
        rec = tp / (tp + fn) if tp + fn else 0.0
        f1 = 2 * prec * rec / (prec + rec) if prec + rec else 0.0
        if best is None or f1 > best["f1"]:
            best = {"threshold": t, "precision": round(prec, 3), "recall": round(rec, 3), "f1": round(f1, 3)}

    ts = time.strftime("%Y%m%dT%H%M%S")
    run_dir = os.path.join(ART, f"oracle_frozen_{ts}")
    os.makedirs(run_dir, exist_ok=True)
    artifact = {
        "timestamp": ts, "model": MODEL, "method": "NLI symmetric contradiction prob",
        "label_source": "RULE-CONSTRUCTED ground truth (NOT human labels) — instrument P/R only; "
                        "domain+human validation is the documented next step",
        "n_pairs": len(data), "n_contradiction": sum(l for _, _, l in data),
        **best,
    }
    with open(os.path.join(run_dir, "validation.json"), "w") as f:
        json.dump({"artifact": artifact,
                   "pairs": [{"a": a, "b": b, "label": l, "score": round(s, 3)}
                             for (a, b, l), (s, _) in zip(data, scores)]}, f, indent=2)
    with open(os.path.join(run_dir, "FROZEN"), "w") as f:
        json.dump(artifact, f, indent=2)
    with open(FROZEN, "w") as f:
        json.dump(artifact, f, indent=2)
    # make the frozen run read-only (Guard 1)
    for root, _, files in os.walk(run_dir):
        for fn in files:
            os.chmod(os.path.join(root, fn), stat.S_IRUSR | stat.S_IRGRP | stat.S_IROTH)
    print(json.dumps(artifact, indent=2))
    print(f"\nFROZEN -> {run_dir} (read-only)")


if __name__ == "__main__":
    sweep_and_freeze()
