"""Spike 0 — in-memory content-addressed claim store.

Claims are stored absolutely (no residual coding). Operations: assert_claim,
supersede, retract. Reference edges are tracked for Gate 1 propagation measurement.
Conflict-flagging via the frozen NLI oracle (lazy, cached). Snapshot reads for
comparing store context vs. flat-pile context (Gate 3).
"""
import json, os, collections

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "data")


class Store:
    """Content-addressed absolute claim store."""

    def __init__(self):
        self._claims = {}        # id -> claim dict (with 'status': active/retracted/superseded)
        self._versions = collections.defaultdict(list)  # canonical_id -> [id, ...]
        self._ref_edges = collections.defaultdict(set)  # id -> set of ids it references (by scope)
        self._scope_index = collections.defaultdict(list)  # scope_key -> [id, ...]
        self._conflicts = []     # list of (id_a, id_b, score)

    # ---- writes ----

    def assert_claim(self, c):
        """Insert a claim; if id already present, skip (idempotent)."""
        cid = c["id"]
        if cid not in self._claims:
            self._claims[cid] = {**c, "status": "active"}
            sk = _scope_key(c["scope"])
            self._scope_index[sk].append(cid)
        return cid

    def supersede(self, old_id, new_claim):
        """Mark old claim superseded, insert new one."""
        if old_id in self._claims:
            self._claims[old_id]["status"] = "superseded"
        new_id = self.assert_claim(new_claim)
        self._versions[old_id].append(new_id)
        return new_id

    def retract(self, cid):
        """Mark claim retracted (preserved for provenance)."""
        if cid in self._claims:
            self._claims[cid]["status"] = "retracted"

    def add_ref_edge(self, from_id, to_id):
        """Record a reference edge (from_id mentions/references to_id's scope)."""
        self._ref_edges[from_id].add(to_id)

    def add_conflict(self, id_a, id_b, score):
        self._conflicts.append((id_a, id_b, score))

    # ---- reads ----

    def active_claims(self):
        return [c for c in self._claims.values() if c["status"] == "active"]

    def claims_for_scope(self, scope_key):
        ids = self._scope_index.get(scope_key, [])
        return [self._claims[i] for i in ids if self._claims[i]["status"] == "active"]

    def get(self, cid):
        return self._claims.get(cid)

    def neighbors(self, cid):
        return list(self._ref_edges.get(cid, []))

    def conflicts(self):
        return list(self._conflicts)

    def snapshot_context(self, query_article, n=20):
        """Return top-n active claims for a query article (store view)."""
        active = [c for c in self.active_claims()
                  if c["prov"]["article"] == query_article]
        return active[:n]

    def flat_context(self, query_article, all_claims, n=20):
        """Return top-n claims from the flat pile for the same article (both snapshots)."""
        matching = [c for c in all_claims if c["prov"]["article"] == query_article]
        return matching[:n]

    def degree(self, cid):
        """Number of outgoing + incoming reference edges for cid."""
        out_deg = len(self._ref_edges.get(cid, []))
        in_deg = sum(1 for edges in self._ref_edges.values() if cid in edges)
        return out_deg + in_deg

    def stats(self):
        total = len(self._claims)
        active = sum(1 for c in self._claims.values() if c["status"] == "active")
        superseded = sum(1 for c in self._claims.values() if c["status"] == "superseded")
        retracted = sum(1 for c in self._claims.values() if c["status"] == "retracted")
        return {"total": total, "active": active, "superseded": superseded,
                "retracted": retracted, "conflicts": len(self._conflicts)}


def _scope_key(scope):
    return scope.lower().strip()


def build_store(claims, oracle=None):
    """Populate store from extracted claims. Pairs old/new snapshots by id and
    supersedes old claims that have a corresponding new claim with the same id
    (same body+scope hash). Detects scope conflicts via oracle if provided.
    Also builds reference edges between co-scoped claims across articles.
    Returns (store, all_claims)."""
    st = Store()

    # group by article
    by_article = collections.defaultdict(lambda: [[], []])
    for c in claims:
        by_article[c["prov"]["article"]][c["snapshot_index"]].append(c)

    for article, (snap0, snap1) in by_article.items():
        snap0_ids = {c["id"]: c for c in snap0}
        snap1_ids = {c["id"]: c for c in snap1}

        # claims only in snap0 -> retract (removed by editors)
        only_old = set(snap0_ids) - set(snap1_ids)
        # claims only in snap1 -> assert new
        only_new = set(snap1_ids) - set(snap0_ids)
        # same id in both -> supersede old with new (content-identical; idempotent)
        in_both = set(snap0_ids) & set(snap1_ids)

        for cid in in_both:
            st.assert_claim(snap0_ids[cid])
            # new snapshot has same hash -> body+scope unchanged; still supersede to
            # update provenance timestamp
            st.supersede(cid, snap1_ids[cid])

        for cid in only_old:
            st.assert_claim(snap0_ids[cid])
            st.retract(cid)

        for cid in only_new:
            st.assert_claim(snap1_ids[cid])

    # build lightweight reference edges: claims within the same article that share
    # scope tokens link to each other (proxy for semantic co-reference)
    active = st.active_claims()
    by_scope = collections.defaultdict(list)
    for c in active:
        sk = _scope_key(c["scope"])
        by_scope[sk].append(c["id"])
    for sk, ids in by_scope.items():
        for i in range(len(ids)):
            for j in range(i + 1, min(i + 4, len(ids))):
                st.add_ref_edge(ids[i], ids[j])

    # conflict detection (expensive: only run if oracle supplied, sample pairs)
    if oracle is not None:
        _flag_conflicts(st, oracle, max_pairs=2000)

    return st, claims


def _flag_conflicts(st, oracle, max_pairs=2000):
    """Check co-scoped active claims for contradictions."""
    by_scope = collections.defaultdict(list)
    for c in st.active_claims():
        sk = _scope_key(c["scope"])
        by_scope[sk].append(c)

    checked = 0
    for sk, group in by_scope.items():
        if len(group) < 2:
            continue
        for i in range(len(group)):
            for j in range(i + 1, len(group)):
                if checked >= max_pairs:
                    return
                ca, cb = group[i], group[j]
                is_contra, score = oracle.contradicts(ca["body"], cb["body"])
                if is_contra:
                    st.add_conflict(ca["id"], cb["id"], score)
                checked += 1
