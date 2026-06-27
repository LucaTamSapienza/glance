"""Spike 0 — view composition: assemble a coherent context from store claims.

Single-fidelity faithful view: just concatenate the active claims for a query
article. Faithfulness check: does the assembled view entail its source claims
(no dropped/hallucinated facts)? Uses NLI entailment direction for check.
"""
import torch
from transformers import AutoTokenizer, AutoModelForSequenceClassification

NLI_MODEL = "MoritzLaurer/DeBERTa-v3-base-mnli-fever-anli"
_nli = None  # lazy singleton


def _get_nli():
    global _nli
    if _nli is None:
        import os
        device = "mps" if torch.backends.mps.is_available() else "cpu"
        tok = AutoTokenizer.from_pretrained(NLI_MODEL)
        model = AutoModelForSequenceClassification.from_pretrained(NLI_MODEL).to(device).eval()
        entail_idx = next(i for i, l in model.config.id2label.items()
                          if "entail" in l.lower())
        _nli = (tok, model, entail_idx, device)
    return _nli


@torch.no_grad()
def _entail_prob(premise, hypothesis):
    tok, model, entail_idx, device = _get_nli()
    x = tok(premise, hypothesis, return_tensors="pt", truncation=True,
             max_length=256).to(device)
    return float(model(**x).logits.softmax(-1)[0][entail_idx])


def compose_view(claims, max_claims=20):
    """Compose a single-fidelity view from a list of claims.
    Returns plain-text context string (bare claim bodies for NLI faithfulness checking)."""
    top = claims[:max_claims]
    return "\n".join(c.get("body", "") for c in top if c.get("body"))


def check_faithfulness(view_text, source_claims, entail_threshold=0.5):
    """Check view faithfulness: no hallucinated sentences (each view line must be
    entailed by some source claim) and no dropped claims (each source claim must
    be represented in some view line).

    The "dropped" check uses per-sentence NLI (source claim body vs each view
    line), not view-as-paragraph, because DeBERTa NLI doesn't reliably score
    multi-sentence paragraphs as premises. The per-sentence check correctly
    handles our verbatim view composition.
    Returns a dict with violation flags.
    """
    violations = []
    dropped = []

    sentences = [s.strip() for s in view_text.split("\n") if s.strip()]

    # Hallucination check: each view sentence must be entailed by some source claim
    for sent in sentences:
        best = max((_entail_prob(c["body"], sent) for c in source_claims), default=0.0)
        if best < entail_threshold:
            violations.append({"sentence": sent[:80], "best_entail": round(best, 3)})

    # Dropped-claim check: each source claim must entail some view sentence
    for c in source_claims:
        body = c["body"]
        best = max((_entail_prob(body, sent) for sent in sentences), default=0.0)
        if best < entail_threshold:
            dropped.append({"claim_id": c["id"], "body": body[:60], "best_entail": round(best, 3)})

    return {
        "violation_free": len(violations) == 0 and len(dropped) == 0,
        "hallucinated_sentences": violations,
        "dropped_claims": dropped,
    }
