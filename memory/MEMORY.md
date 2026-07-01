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

- **Session start** — read this index, then what you need. glance itself is the
  cheap way in: `./glance --context "your question" memory/ --budget 2000`
  (synchronously — never with `&`), or `--section "memory/status.md#Open"`.
- **After non-trivial work** — fold the change into the right note: update
  [[status]] in the same change; add a dated entry to [[decisions]] when you
  settled something a future session could second-guess; add to [[lessons]]
  only what was empirically surprising. `./glance --edit` makes surgical
  updates.
- **Curate, don't append.** Rewrite stale lines, merge duplicates, delete the
  superseded — git history is the archive. Keep each note under ~150 lines.
- **One `##` heading per entry.** Headings are glance's retrieval unit —
  `--context` and `--section` address them — so an entry without its own
  heading is invisible to budgeted retrieval.
- Absolute dates only (write 2026-07-01, never "today").
