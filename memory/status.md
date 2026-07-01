# Status

> Last updated: 2026-07-02. What's done, what's in flight, what's open.
> Rules and invariants live in AGENTS.md, not here.

## On main

**User-side** is a complete reader/editor: Reader / Insert / Split modes;
search (`/ n N`); TOC (`t`); atomic save + kqueue live-reload (clean buffers
adopt external edits, dirty ones get an `r`/`k` conflict prompt); charwise and
linewise selection with clipboard yank; `[[wikilinks]]` / backlinks (`b`) /
graph explorer (`Ctrl-G`) / fuzzy switcher (`Ctrl-P`); twelve themes with a
live picker (`T`) and `~/.config/glance/config`; per-language syntax
highlighting; bordered aligned tables; inline images with clipboard-image
paste; key-legend sidebar (`?`); trackpad scrolling + progress HUD; exact
offset-based reader↔editor cursor sync; HTML export (`glance-render --html`)
and PDF via a detected converter (`glance --export`).

UX batch merged 2026-07-02 (all confirmed live): a piped stdin renders to
stdout (`cat x.md | glance`, like glance-render); trackpad/wheel scrolling
works in all three modes; word motion is punctuation-aware (macOS
end-of-word semantics) in the editor **and** the Reader (Alt/Ctrl+arrows =
word, Cmd+arrows / Ctrl-A/E = line start/end); `keyboard = enhanced` config
key opts into the kitty protocol where a terminal needs it, with a
stack-clearing teardown (see [[lessons]] for the iTerm2 story).

**Agent-side** (M1–M4 of docs/DESIGN.md) is shipped: bounded reads
(`--outline`, `--section`, `--neighbors`, `--backlinks`, `--since`,
`--links`, `--graph`), budgeted retrieval (`--context` — BM25 + link-graph
prior, diversity, coarse-to-fine, truncation manifest, token receipt),
surgical writes (`--edit`, `--set-frontmatter`), and the MCP server
(`glance mcp`). Hardened after an adversarial review
(docs/archive/REVIEW.md): JSON parser depth cap, setext-aware edits, fence
tracking, frontmatter escaping, surrogate-pair decoding, UTF-8-validated
output.

**27 test suites**, green locally and in CI (macos-latest). `make test`
probes AddressSanitizer and falls back to UBSan alone where asan can't start
(macOS 26 deadlock — see [[lessons]]).

## In flight (branches)

- **feat/semantic-minilm — complete on the branch, not merged.** The real
  semantic tier: all-MiniLM-L6-v2 (fp16, via llama.cpp) behind the `Embedder`
  seam, persistent `.glance/` embedding cache, model download-on-first-use,
  k-hop graph-expansion retrieval (zero-lexical neighbours finally surface).
  Supersedes DESIGN.md §11's "next two". Spike numbers and ship decisions:
  [[decisions]]. Open before merge: the llama.cpp dependency story on main
  (the branch tracks `third_party/llama.cpp` as a submodule).
- **feat/wysiwyg-inline — ON HOLD.** Inline WYSIWYG editing (markup renders
  in place as you type, Obsidian-style), collapsing Reader/Insert into one
  mode. The big user-side bet; parked, not abandoned. glance's durable edge
  stays the agent-side token-saving layer.

## Open

- **User-side residuals:** inline images decode on every frame (a
  persistent-plane cache is the right fix); flip the enhanced keyboard
  protocol on by default once `keyboard = enhanced` has seen field testing;
  remote (`http`) images aren't fetched; wide tables overflow rather than
  wrap; display width is one column per codepoint (wide/zero-width chars TBD).
- **Agent-side:** the token receipt is a heuristic (`max(bytes/4, words)`) —
  a real tokenizer or a calibration would make the saved-% exact; the MCP
  server advertises only `tools` (no `resources`/`prompts`).
- **Security residual** (from the review): relative `../` image/link targets
  aren't confined to the vault.
- **Product:** the `glance` name collides with the OpenStack CLI — decide
  (rename? Homebrew tap name?) before packaging.
