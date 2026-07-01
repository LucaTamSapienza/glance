# glance Agent-Native Memory Layer (M1–M4) — Review Report

> **Archived 2026-07-01.** Adversarial review of the M1–M4 layer as of
> 2026-06-18. Every confirmed finding below was fixed on main in the follow-up
> hardening pass (JSON depth cap, setext-aware edits, fence tracking,
> frontmatter escaping, surrogate-pair decoding, number-grammar validation,
> emit hardening). Kept as the record of the untrusted-input boundary
> analysis. Current state: [`memory/status.md`](../../memory/status.md).

## 1. Verdict

The memory layer is well-structured and the "one model, two sinks" discipline holds: retrieval, section addressing, JSON, and edit are cleanly separated and individually testable. Health is good but the **untrusted-input boundary is soft**. The single serious defect is an unbounded JSON parser recursion that crash-DoSes the MCP server; clustered around it is a family of JSON/MCP conformance gaps (surrogate pairs, `strtod` leniency, non-UTF-8 passthrough, `id` echo) and a write-path integrity gap (raw frontmatter, ATX-only edit scanning, unknown-op→append). None corrupt the happy path, most need crafted or malformed input, and the OOM-class findings are effectively unreachable under macOS overcommit. The bulk of the remaining findings are honest low-severity cleanups and clarity nits.

## 2. Confirmed bugs (ranked by severity)

### High
- **Unbounded parser recursion** — `src/json.c:102-140,152-163`. `parse_value`→`parse_array`/`parse_object`→`parse_value` has no depth cap; `mcp_handle_line` (`mcp.c:252`) feeds raw stdin in, so a line of a few thousand `[` SIGSEGVs the server. Fix: thread a depth counter, reject past ~200 levels, add a deep-nesting test.

### Medium
- **Fenced-code tracking ignores fence type/length/info-string** — `src/edit.c:86-91,103`. `is_fence` toggles one bool for either ``` or `~~~`; a `~~~` content line inside a ```-block (or a closing line with an info string) flips `fence` off, so `#` lines inside code parse as headings and `agent_edit` writes to the wrong section. Fix: track opening char + run length; close only on same char, ≥ length, no info string.
- **Frontmatter value/key written raw** — `src/edit.c:166-167,177,187` (agent write path via `agent.c:567`). No escaping/validation: a newline injects YAML lines, `x\n---` closes the block early, a value like `Notes: part 1` misparses — silent note corruption from CLI/MCP input. Fix: reject embedded newlines, quote/escape YAML-significant values, document the contract.
- **`\u` escapes don't combine UTF-16 surrogate pairs** — `src/json.c:44-55`. Each `\uXXXX` is encoded alone, so non-BMP chars (emoji) decode to two invalid 3-byte sequences. Fix: combine hi+lo surrogate, reject lone surrogates.
- **Number parsing too lenient** — `src/json.c:76-84`. `strtod` accepts `inf`/`Infinity`/`NaN`/`+1`/`01`/`.5`/`1.`/hex floats that JSON forbids; a non-finite `id` later echoes as literal `inf` (invalid JSON-RPC). Fix: validate JSON number grammar around `strtod`, reject `!isfinite`.
- **`emit_id` invalid JSON / precision loss** — `src/mcp.c:34-45`. `(long long)d` on a non-finite or >2^53 double is UB (the repo runs UBSan), and `%g` emits `inf`/`nan`; large int ids lose digits, violating verbatim-echo. Fix: guard with `isfinite()`, ideally preserve the raw id token.
- **`emit_jstr` passes bytes ≥0x80 unescaped** — `src/mcp.c:19-31`. Captured tool output from arbitrary (possibly non-UTF-8) vault files is copied verbatim into the JSON string → invalid UTF-8 JSON. Fix: validate UTF-8 and replace invalid sequences with U+FFFD (don't blanket-escape valid multibyte).
- **`--edit` maps unknown OP → append** — `src/main.c:166`. Anything not `insert`/`replace` becomes `EDIT_APPEND` (0); a typo/`delete`/`Append` silently appends to disk. Fix: validate against the closed set, error out on no match.
- **`split_lines` realloc unchecked** — `src/edit.c:53-54`. `v = realloc(v, …)` then immediate write → NULL-deref crash + leak on OOM, breaking the documented "NULL on OOM" contract (OOM-only, hence medium). Fix: temp pointer, free + return NULL, callers treat as OOM.

### Low (real bugs, downgraded from the original medium)
- **`--context` consumes a value-less flag as the vault dir** — `src/main.c:198-202`. `--budget` with no value falls into `else dir = argv[i]`, clobbering the real dir → confusing failure on a malformed CLI. Fix: recognize known flags even when their value is missing; reject a second positional.
- **Under-supplied subcommand falls through to open-as-file** — `src/main.c:128-204,212-225`. `glance --outline` with too few args reaches `path = argv[1]`, ENOENT is treated as "new empty file", launching the TUI titled after the flag. Fix: reject known-subcommand tokens whose arity isn't met with usage + exit 2.
- **Slug drops all non-ASCII** — `src/section.c:30-51`. `naïve`→`nave` collides with `Nave`; fully non-ASCII headings slug to `""` and are slug-unaddressable. Limited because the exact-text path still matches Unicode. Fix: decode UTF-8 / keep Unicode alnum, or document ASCII-only.
- **Duplicate/slug-colliding headings: only first addressable** — `src/section.c:63-64`. `section_find` breaks on first match, no occurrence index or ambiguity report. Fix: count matches and report ambiguity, or accept a `-1`/`-2` suffix.
- **`section_text` double-pass size/copy divergence** — `src/section.c:108-127`. A `line_text` NULL-in-sizing/success-in-copy split overflows the buffer; near-unreachable under macOS overcommit, but the double malloc is real waste. Fix: single pass, size from `runs[j].len+1`.
- **`fit()` conflates a zero-token candidate with "doesn't fit"** — `src/context.c:22-26,59-60,71-72`. The `cost==0 && !unlimited` guard skips a free 0-token candidate under any finite budget but picks it when budget==0 (asymmetry); it lands in the truncation manifest. Synthetic-only (real pipeline never yields `full_tokens==0`). Fix: return an int fit-flag + separate `*cost` out-param.
- **`mention_line` backlink snippet matches stem as bare substring** — `src/agent.c:222-244,281`. Case-insensitive substring scan can point the "why it links here" snippet at an unrelated word or a code-fence line. Linkage itself is correct. Fix: prefer link syntax (`[[stem`, `](…stem`) / word boundary.
- **`sec_push` derefs possibly-NULL section text/anchor** — `src/agent.c:343-344,384,393,491`. No NULL guard where sibling exporters all guard (`agent.c:97/104/65/73`); OOM-only, and `sec_push` doesn't harden its own realloc anyway. Fix: treat NULL as `""`/0 tokens.
- **`\u0000` yields an interior NUL** — `src/json.c:54,62`. Decoded inline; every C-string consumer truncates. Fix: reject `cp==0` in `parse_string_raw`.

## 3. Quality cleanups (non-bug, worthwhile)

- **Stale `note_seen` comments in `context_plan`** — `src/context.c:35-36,50`. Describe a nonexistent array + a dangling question fragment; actual dedup is the inline scan at lines 55-56. Delete or rewrite. (Reported twice; same issue.)
- **Dead clamps in `receipt_saved_pct`** — `src/receipt.c:31-37`. Guard bounds `pct` to [0,99]; the `<0`/`>100` clamps are unreachable. Drop or replace with an assert.
- **Slug folds `_`→`-`** — `src/section.c:37`. Diverges from the "GitHub-style slug" claim it advertises (`section.h:31-32`). Preserve `_` or reword the comment.
- **`section_abstract` can return only sub-headings** — `src/section.c:91-106`. A body opening with a sub-heading yields heading+subheading, no prose, contradicting the documented contract. Skip `.heading>0` lines until real prose.
- **`emit_block` empty payload → 3 newlines** — `src/edit.c:31-38`. REPLACE-to-empty emits two blank lines; cosmetic. Skip the final `\n` when text is empty.
- **Indented/space-before-colon frontmatter key not matched** — `src/edit.c:184-185`. `key : x` isn't recognized → duplicate key inserted. Relax the space-before-colon only (indentation = YAML nesting, leave unmatched).
- **`bm25_add` leaves a partial doc on mid-tokenization OOM** — `src/bm25.c:165-188`. `doc_push` runs before tokenizing; non-zero return leaves a half-indexed doc. Document "discard index after non-zero", or roll back.
- **`bm25_search` allocates on the no-match path** — `src/bm25.c:210-211,248-265`. With `nqt==0` it still allocs/scans `nd`. Early-return 0 (freeing `score`/`qt`).
- **MCP capture has no error handling** — `src/mcp.c:50-69`. Unchecked `dup`/`dup2`/`tmpfile`; on `tmpfile==NULL` tool output corrupts the JSON-RPC stream and `cap_end` returns `""` (silent success). Arguably medium. Fail-fast with `-32603`.
- **Batch (top-level array) silently dropped** — `src/mcp.c:238-257`. No `JSON_ARR` branch → no response. Emit `-32600` (current MCP spec dropped batching, so low).
- **JSON `O(n²)` child growth** — `src/json.c:87-100`. `realloc` by +1 per child; grow geometrically (keep `keys` capacity in lockstep).
- **FNV bucket uses low bits with power-of-two dim** — `src/embed.c:29`. `h % 256` masks FNV-1a's weak low bits; xor-fold high bits or use a multiplicative reduction.
- **Tokens >64 bytes truncated/collide** — `src/embed.c:48`. Long URLs/identifiers share a 64-byte prefix; hash incrementally.
- **Unchecked malloc on agent hot paths** — `src/agent.c:166-168,337,403,439-441,470-471`. Agent-facing entry points; lower modules already use the realloc-into-temp idiom (`vault.c`, `graph.c`). Mirror it and bail with non-zero/error JSON.

## 4. Soft-spot audit (HANDOFF known gaps)

- **edit.c is ATX-only, no setext** — `src/edit.c:93-112`: **CONFIRMED, and worse than "low".** Setext headings are invisible both as edit targets *and* as section boundaries. The concrete harm is **silent data loss on REPLACE**: a setext sibling heading inside an ATX section's body isn't seen as a boundary, so the drop range runs to the next ATX heading and deletes the setext section and its content. Setext is first-class everywhere else (preprocess.c, toc.c), so this is an internal inconsistency. Rated **medium**. Fix: a setext detector (a `=`/`-`-only line following a non-blank, non-list paragraph line) applied to both target-match and boundary scans, with REPLACE tests — and require the preceding paragraph line so a bare `---` thematic break isn't misread.
- **Fenced-code headings ignored** — CONFIRMED handled correctly for the single-fence case (`is_fence` toggles), but the `~~~`/info-string gap above (§2 medium) means mixed/nested fences still break boundaries.
- **Frontmatter raw write** — CONFIRMED (see §2 medium); the soft-spot note and the write-safety finding are the same defect.
- **Stale `context_plan` comments** — CONFIRMED, cosmetic (see §3).

Net: the HANDOFF's gaps are real. The setext gap is the one the handoff under-rated — it is a write-path data-loss edge case, not a cosmetic limitation.

## 5. Level-up roadmap (ranked by impact / effort)

1. **`glance ask` — one-shot retrieve→LLM→answer (CLI)** · *impact high · ~1–2 days*. Wrap `agent_context` and shell out to `curl` (precedent: `clipboard.c`) against the Anthropic Messages API; render the Markdown answer through glance's own renderer and print a two-part token receipt. The shareable receipt screenshot is the growth-loop wedge; keep the network call opt-in, env-key-gated (`-K -` so the key never hits argv), and out of the core binary. Main cost is refactoring `agent_context` to return a struct instead of only printing JSON.
2. **Persistent incremental BM25 cache under `.glance/`** · *impact high · 6–8 days (2-day lexical-only spike first)*. Cache the query-independent finalized BM25 index + section catalog + per-file outlinks, invalidated by a stat() mtime/size ledger (not fswatch); re-extract text on demand for picked notes only. Split `agent_context` into `ctx_index_open` + the existing query path; add `--reindex`/`--no-cache`. Warm runs skip ~all file reads/parses for a realistic ~10–50x speedup; the real work is adding serialize/tombstone-delete/append to the monolithic `bm25.c`.
3. **Graph-expansion retrieval (1-hop neighbour injection)** · *impact medium · ~0.5 day*. Insert a bounded injection stage between the additive graph prior and the candidate loop (`agent.c:467`): seed on strong lexical hits, pull in zero-lexical 1-hop neighbours at a discounted, provenance-tagged score, capped (SEED_MAX/INJECT_MAX, one representative section per neighbour). This is what actually delivers "graph beats grep"; default injected candidates to abstract-only (one `fit()` field) and down-weight hubs by in-degree.
4. **Real MiniLM-class encoder behind the Embedder seam** · *impact medium · 5–7 days*. Add `embed_minilm.c` using llama.cpp/ggml (GGUF carries its own tokenizer — the deciding factor for a C codebase; Metal on by default), behind a `GLANCE_SEMANTIC` flag, falling back to today's hashing embedder when the model is absent; add a `.glance/embed/` cache. Pairs with roadmap #2 (cache the vectors). Main risks: llama.cpp API churn (pin a commit) and matching MEAN-pool+L2 cosine to a sentence-transformers reference.

## 6. Recommended next 3 actions

1. **Cap JSON parser depth** (`json.c`) — the only crash-on-untrusted-input bug; ~10 lines + a deep-nesting test, closes the MCP DoS.
2. **Harden the write path as one batch**: validate `--edit` OP membership (`main.c:166`), escape/reject frontmatter values (`edit.c:166-187`), and add setext awareness to `edit_section` (`edit.c:93-112`). These three are the agent-facing data-integrity defects, including the under-rated setext REPLACE data-loss case.
3. **Tighten JSON/MCP conformance**: combine surrogate pairs + reject lone surrogates (`json.c:44-55`), validate the number grammar / reject non-finite (`json.c:76-84`) and guard `emit_id` with `isfinite()` (`mcp.c:34-45`), and sanitize non-UTF-8 in `emit_jstr` (`mcp.c:19-31`) — together they stop glance from emitting invalid JSON-RPC to strict clients.

## 7. Fixed in branch `feat/review-fixes`

The HIGH defect and the entire write-path + JSON/MCP-conformance cluster (the
recommended next-3-actions) are fixed, each with a test:

- **HIGH** — JSON parser depth cap (`JSON_MAX_DEPTH 200`); the deep-nesting MCP
  DoS now returns a clean `-32700` and the server survives (verified).
- **Write path** — `--edit`/`vault_edit` reject an unknown op; `edit_frontmatter`
  rejects newlines and double-quotes YAML-unsafe values; `edit_section` is now
  **setext-aware** (target + boundary), closing the REPLACE data-loss case; the
  fence tracker distinguishes ``` from `~~~` (char + run length + info string);
  `split_lines` checks `realloc`.
- **JSON/MCP conformance** — `\u` combines UTF-16 surrogate pairs and rejects
  lone surrogates and interior NUL; JSON number grammar validated and non-finite
  rejected; `emit_id` guarded with `isfinite()`; `emit_jstr` validates UTF-8 and
  emits U+FFFD for invalid bytes.

The remaining low-severity items and the §3 cleanups are left as follow-ups; the
§5 level-up roadmap is unchanged. `make test`: 23 suites green under ASan/UBSan.