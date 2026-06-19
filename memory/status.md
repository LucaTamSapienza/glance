# Status

> Last updated: 2026-07-11 (M5 merged: doctor + seed + the dogfood
> hardening — 35 modules, 29 suites). What's done, what's in flight, what's
> open. Rules and invariants live in AGENTS.md, not here.

## On main

**User-side** is a complete reader/editor: Reader / Insert / Split modes;
search (`/ n N`); TOC (`t`); atomic save + kqueue live-reload (clean buffers
adopt external edits, dirty ones get an `r`/`k` conflict prompt); charwise and
linewise selection with clipboard yank; `[[wikilinks]]` / backlinks (`b`) /
graph explorer (`Ctrl-G`) / fuzzy switcher (`Ctrl-P`); twelve themes with a
live picker (`T`) and `~/.config/glance/config`; syntax highlighting for 9
languages; bordered aligned tables; inline images with clipboard-image paste
(`Ctrl-V`); key-legend sidebar (`?`); trackpad scrolling + progress HUD; exact
offset-based reader↔editor cursor sync; piped stdin renders to stdout;
punctuation-aware word motion in editor **and** Reader; `keyboard = enhanced`
opt-in (kitty protocol) with a stack-clearing teardown ([[lessons]]); HTML
export (`glance-render --html`) and PDF via a detected converter
(`glance --export`).

**Agent-side** (M1–M4 of docs/DESIGN.md) is shipped: bounded reads
(`--outline`, `--section`, `--neighbors`, `--backlinks`, `--since`,
`--links`, `--graph`), budgeted retrieval (`--context` — BM25 + link-graph
prior, diversity, coarse-to-fine, truncation manifest, token receipt),
surgical writes (`--edit`, `--set-frontmatter`), and the MCP server
(`glance mcp`, tools reusing the exact CLI exports). Hardened after an
adversarial review (docs/archive/REVIEW.md): JSON parser depth cap,
setext-aware edits, fence tracking, frontmatter escaping, surrogate-pair
decoding, UTF-8-validated output. M5 (2026-07-11, PRs #24/#25) mechanized
the memory protocol: `--doctor` / `vault_doctor` — the hygiene report with
dogfood-set semantics (colon/paren markers outside fences, dangling across
embeds/anchors/relative `.md` links, distinct-note degree, `unreadable`),
`summary.clean` and exit 0/2/1 (a CI gate) — and `--seed` / `vault_seed`,
the one-command brain scaffold (additive `memory/` skeleton, repo facts +
fill plan + `wire_snippet` as JSON, doctor exit 0 as the done-check), plus
`--edit before` for surgical dated-log backfills.

**Verified 2026-07-11:** 29 test suites green (UBSan; the ASan probe story:
[[lessons]]) locally and in CI (macos-latest); no TODO/FIXME markers in src/
(seed.c's template fill-markers are string content, not source markers).
Untested by design: tui.c (~2.3k lines, hand-verified), clipboard.c, the two
entry points. The release artefact is darwin_arm64 only.

## In flight (branches)

- **feat/semantic-minilm — complete on the branch, not merged.** The real
  semantic tier: all-MiniLM-L6-v2 (fp16, via llama.cpp) behind the `Embedder`
  seam, persistent `.glance/` embedding cache, model download-on-first-use,
  k-hop graph-expansion retrieval. Supersedes DESIGN.md §11's "next two".
  Spike numbers and ship decisions: [[decisions]]. Rebased onto post-M5 main
  on 2026-07-21 (its STATUS.md/context.md edits folded into AGENTS.md + this
  note; verified end-to-end that day: build, suites, model fetch, warm cache
  86 ms, k-hop). Open before merge: the llama.cpp dependency story on main
  (the branch tracks `third_party/llama.cpp` as a submodule).
- **feat/wysiwyg-inline — ON HOLD.** Inline WYSIWYG editing, collapsing
  Reader/Insert into one mode. The big user-side bet; parked, not abandoned.
- **feature/claim-store — Eddie's research spike, analyzed 2026-07-10.**
  Python-only and purely additive (spike/ + two specs; no C touched): a
  pre-registered test of knowledge-as-claims vs a flat document pile over
  Wikipedia edit streams. Gate 3 (consistency advantage) replicated 3×;
  Gate 1 (hot-node merge propagation) never measured — the frozen TCP stress
  test is still owed. Its own pre-registered verdict: NO-GO for building the
  C modules. Do not merge as-is; the verdict and what to salvage:
  [[decisions]].

## Open

- **User-side residuals:** inline images decode on every frame (persistent
  planes are the right fix); flip enhanced keyboard on by default only after
  field testing; remote (`http`) images aren't fetched; wide tables overflow
  rather than wrap; display width is one column per codepoint; task-list
  checkboxes render only in the HTML export (render.c ignores `is_task` —
  terminal sinks show a plain bullet); raw HTML is dropped by the terminal
  sinks but passes verbatim into the HTML export; selection/cursor/search-hit
  and graph-explorer colors are hardcoded outside the theme (`hit_fg` not
  settable from config); silent fixed caps (back-stack 64, backlinks/graph
  panels 256, 16 image planes).
- **Agent-side:** the token receipt is a heuristic (`max(bytes/4, words)`) —
  a real tokenizer or a calibration would make the saved-% exact; the MCP
  server advertises only `tools` (no `resources`/`prompts`); BM25 terms are
  ASCII-only, so non-ASCII text is invisible to the lexical tier (the
  `Embedder` seam is the planned way out); with `--semantic` nearly every
  section scores > 0, inflating the `truncated` manifest; `HL_TYPE` is never
  emitted (every `LangSpec.ty` is NULL) though every theme defines its color;
  brain-scaffold phase 2 is open — the plugin skill that executes seed's
  fill plan (seed → fill via `--edit` → doctor exit 0).
- **Doc rot (found by the 2026-07-10 re-read):** DESIGN.md §9/M3 still says
  "model pending the benchmark" (superseded by feat/semantic-minilm — update
  at merge); tui.c's header comment still describes the pre-vi two-mode UI;
  editor.c keeps a stale duplicate `ed_wordsep` comment and the dead
  `Editor.xoff` field.
- **Security residual** (from the review): relative `../` image/link targets
  aren't confined to the vault.
- **Product:** the `glance` name collides with the OpenStack CLI — decide
  (rename? Homebrew tap name?) before packaging.

