# Legend sidebar — design

**Date:** 2026-06-16
**Status:** Implemented (2026-06-17, branch `feature/legend`)
**Scope:** A hidable, content-compressing keybinding legend for the glance TUI.

## Implementation notes (as built)

- **Reader-only.** During design we considered showing Insert-mode keys when the
  panel was open in Insert. In practice `?` is a literal character while editing
  (Insert/Split), so the panel can only be opened from the Reader — it is now a
  Reader feature and lists the Reader/Vault/edit-entry keys. The "context per
  mode" idea is therefore moot for v1; revisiting it would require a dedicated
  chord in the editor.
- Pure layout (width split, fallback test, aligned row formatting) lives in
  `src/legend.c` with `tests/legend_test.c`; `tui.c::draw_legend` does the
  notcurses drawing. `content_cols()` feeds the reduced width to `rerender()`
  and every reader draw path (doc lines, cursor, selection, hits, images).
- Constants: `LEGEND_W = 30`, `LEGEND_MIN_CONTENT = 24`, `LEGEND_KEYCOL = 8`.
- Narrow-window fallback to the centered overlay is implemented both at toggle
  time and on resize (`rerender` auto-closes the panel if it no longer fits).

## Problem

New users have no in-place way to learn glance's vi-style keybindings while
reading. The only help today is a centered modal overlay (`?` → `draw_help`,
`tui.c:754`) that covers the document and is dismissed by any keypress. It can't
stay open while you navigate.

## Goal

A panel that:
- toggles open/closed with `?` (and closes on `Esc`),
- **reflows** the document into the remaining width rather than overlaying it, so
  nothing is covered,
- shows keys relevant to the **current mode** (context-sensitive),
- looks intentional: a rounded, padded, palette-aware frame.

## Non-goals (v1)

- A third reflow column inside Split mode (Split already uses two panes).
- Reader↔editor cursor-exactness changes (out of scope; unchanged).
- Theme selection (tracked separately).

## UX / behavior

- `?` toggles `a->legend`. `Esc` also closes it.
- **The panel always advertises how to close itself.** Its bottom edge shows a
  persistent dismissal hint — a footer reading `Esc · ? close` rendered in the
  border/dim style — so a user who opened it with `?` is never left wondering how
  to get rid of it. This hint is part of the frame, not an entry in the scrolling
  key list, so it stays visible regardless of content.
- When open in **Reader**, the document re-wraps into `cols - LEGEND_W` columns
  and the panel occupies the right-hand `LEGEND_W` columns.
- The panel lists context-sensitive keys:
  - **Reader:** navigation + vault keys (`j/k`, `g/G`, `Ctrl-D/U`, `/`, `n/N`,
    `t`, `Enter`, `-`/`Ctrl-O`, `b`, `Ctrl-G`, `i`, `e`, `Ctrl-S`, `:w/:q`, `?`).
  - **Insert:** editing keys (`Esc`, `Ctrl-S`, `Ctrl-V` paste image, bracket
    auto-close).
- Rows are aligned `key → action` pairs, drawn from a single source of truth
  shared with the existing help text.

## Layout / reflow mechanism

Reuse the width-division approach Split mode already uses (`draw_split`, and
`render_doc_at(..., rw, ...)` at `tui.c:606`).

- `LEGEND_W = 30` (matches `TOC_W`).
- New helper `content_cols(a)` = `a->cols - (a->legend ? LEGEND_W : 0)`.
- `rerender()` (`tui.c:662`) renders the Doc at `content_cols(a)` instead of
  `a->cols`, so the text genuinely re-wraps to the narrower column.
- `draw_reader()` (`tui.c:493`) draws document lines into `content_cols(a)`
  (currently the hardcoded `a->cols` at `tui.c:502`), then draws the panel from
  column `content_cols(a)` to `a->cols`.
- `NCKEY_RESIZE` (`tui.c:1400`) already calls `rerender()`, so resize recomputes
  the wrap width automatically with no extra handling.

## Styling

- Rounded frame: `╭ ╮ ╰ ╯` corners, `─`/`│` edges, dim border color.
- One-space inner padding; a titled top edge (e.g. `╭─ keys ─╮`).
- Panel background + foreground derived from the existing dark/light palette
  (the same `a->dark` RGB choices used by `draw_help`/TOC today).

## Edge cases

- **Narrow terminal:** if `content_cols(a)` would drop below `MIN_CONTENT` (~24
  columns), `?` falls back to the existing centered overlay instead of
  reflowing, so small windows stay usable.
- **Split mode:** no room for a third column — `?` shows the context-sensitive
  list as the centered overlay (documented v1 boundary).
- **Open legend + mode switch:** entering Insert keeps the panel and swaps its
  contents to the Insert key list; entering Split falls back to the overlay.

## Testing

- The reflow math is pure and unit-tested: `content_cols()` with the legend
  on/off, and the `MIN_CONTENT` fallback threshold.
- `make test` must stay green and ASan/UBSan-clean.

## Docs to update (same change)

- `README.md` — `?` description + a line on the reflowing panel.
- `context.md` — Current status + date stamp.
- `STATUS.md` — module map / feature list.
- Auto-memory `project_glance_c_autonomous_plan.md` — plan / known-gaps.

## Decided defaults

- `LEGEND_W = 30`.
- Panel on the **right**; document stays left-aligned.
- Split mode keeps the centered overlay (no third column in v1).
