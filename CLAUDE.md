# CLAUDE.md

@AGENTS.md

Everything canonical — what the project is, build/test, architecture,
invariants, conventions, and the memory protocol — is in `AGENTS.md`, imported
above. It deliberately has no copy here; if the two ever disagree, `AGENTS.md`
wins and this file is the bug.

Claude-specific notes only:

- This repo doubles as a **Claude Code plugin** (`.claude-plugin/`,
  `commands/`, `skills/`). The commands shell out to the *installed* `glance`,
  so run `make install` before exercising them.
- Project state lives in-repo at `memory/` (protocol in AGENTS.md). Prefer it
  over per-user auto-memory notes about this project; keep auto-memory for
  user preferences and environment quirks, not project status.
