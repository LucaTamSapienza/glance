# Memory

Distilled, curated notes on the state of glance — the repo's own memory, kept
the way glance tells agents to keep one: small bounded notes, wikilinked,
rewritten in place rather than appended to. Anyone (human or agent) picking up
work reads this first; the *rules* for working here live in
[AGENTS.md](../AGENTS.md).

- [[status]] — what's on main, what's in flight, what's open
- [[decisions]] — dated choices and the why behind them
- [[lessons]] — hard-won environment/tooling facts (read before chasing a hang)
- [[history]] — how the project got here (the arc, milestone by milestone)

## Protocol (distill, don't accumulate)

Read this index first; then ask, don't browse — `./glance --context "your
question" memory/ --budget 2000` (synchronously — never with `&`), or
`--section "memory/status.md#Open"`.

**Update triggers — if a row fired, write in the same change, not later:**

| You just… | Then… |
|---|---|
| shipped / merged / changed behaviour | rewrite the touched lines of [[status]] (On main · In flight · Open) |
| settled something a future session could second-guess | dated entry in [[decisions]], with the why |
| were empirically surprised (a hang, a leak, a wrong assumption) | entry in [[lessons]] — surprises only, never how-tos |
| landed a milestone, opened or closed a branch | one line in [[history]] |
| only fixed typos | nothing |

**Delete triggers — stale memory is worse than none:**

- a line that main now contradicts → rewrite it (git history is the archive);
- a decision that stopped informing anything → drop its entry;
- a note past ~150 lines → distill it back under.

**Mechanical check — run after any vault edit and fix what it flags:**
`./glance --doctor memory/` (MCP: `vault_doctor`) flags oversized (>150
lines) and stale (>45 days) notes, orphans, dangling `[[wikilinks]]`, and
leftover TODO markers.

**Form:** one `##` per entry — headings are glance's retrieval unit, an entry
without its own heading is invisible to `--context`/`--section`; absolute
dates only (write 2026-07-10, never "today"); wikilink the notes you mention;
`./glance --edit` for surgical updates.

