# Trackpad scroll + reading-progress avatar — design

**Date:** 2026-06-17
**Status:** Implemented (2026-06-17, branch `feature/legend`)
**Scope:** Mouse/trackpad scrolling in the Reader, plus a thin top-right
reading-progress readout with a dots-ring spinner that animates while scrolling
and subtly spins down when you stop.

## Problem

The Reader scrolls only by keyboard (the viewport is glued to the block cursor),
and the only position cue is `Ln 12/340` in the status bar — a number, no
trackpad scrolling and no at-a-glance sense of progress.

## Goals

1. **Trackpad / wheel scrolling** in the Reader that feels smooth.
2. A **thin** reading-progress indicator (no dedicated row): `◟ 42%` in the
   top-right corner.
3. A **dots-ring spinner** (`◜ ◠ ◝ ◞ ◡ ◟`) that animates only while scrolling and
   does a **subtle spin-down** (a few eased frames) when scrolling stops, then
   rests on a neutral glyph (`◌`).

## Non-goals (v1)

- Pixel-smooth scrolling (impossible in a cell terminal — content moves by whole
  rows; trackpad event frequency supplies the smooth feel).
- Scrolling inside Split mode's preview pane (later).
- A draggable scrollbar / click-to-seek.

## Behavior

- **Scroll event → ride-along (decision #1, chosen):** a scroll moves the
  viewport *and* carries the block cursor the same number of lines, so the cursor
  keeps its screen position and stays visible. This preserves the existing
  cursor-centric invariants (`reader_scroll` won't fight the move). Step: **1
  line per scroll event** (smoothest for trackpad; a single wheel notch ticks one
  line).
- **Progress readout:** always visible, thin, top-right of the content area:
  `<glyph> NN%`. Percent tracks the cursor line — `0%` at the top, `100%` at the
  last line. Anchored to the right edge of the *document* area (`content_cols`),
  so it sits just left of the legend panel when that is open.
- **Spinner:** while scrolling, the dots-ring advances; when scrolling stops it
  runs a short, subtle spin-down (~4 frames over ~320 ms) and settles on `◌`.

## Architecture

Pure, testable logic in a new module; notcurses/timing wiring in `tui.c`.

**`src/progress.{c,h}` (pure, no notcurses/md4c, unit-tested):**
- `int progress_percent(int line, int nline)` — reading percent, clamped 0..100
  (`nline <= 1` → 100).
- `void progress_scroll(int *top, int *line, int dir, int nline, int body)` — the
  ride-along step: moves both `top` and `line` by `dir`, clamps `top` to
  `[0, max(0, nline-body)]` and `line` to `[0, nline-1]`.
- `const char *progress_spinner(int frame)` — frame `% 6` of the dots ring
  (`◜ ◠ ◝ ◞ ◡ ◟`).
- `#define PROGRESS_REST "◌"` — the neutral rest glyph.

**`tui.c`:**
- **Enable mice:** after `notcurses_init`, `notcurses_mice_enable(nc,
  NCMICE_SCROLL_EVENT)`. **Disable on every teardown path** (mirroring the
  existing `term_kbd_reset` discipline) so no mouse-tracking sequences leak to the
  shell.
- **Handle** `NCKEY_SCROLL_UP` / `NCKEY_SCROLL_DOWN` in `handle_reader`: call
  `progress_scroll(...)`, then `reader_clamp_cursor` for the column, and mark the
  spinner active (advance a frame, push the settle deadline).
- **App state:** `int spin_frame; long long spin_until_ms;` (monotonic deadline).
- **Settle animation (loop change):** the event loop blocks on `poll(…, -1)`.
  While `spin_until_ms > now`, poll with a short timeout (`FRAME_MS ≈ 80 ms`); on
  a timeout wake, advance `spin_frame` and redraw; once past the deadline, rest.
  Add a `now_ms()` helper (`clock_gettime(CLOCK_MONOTONIC)`). The non-pollable
  `infd < 0` fallback keeps blocking (spinner just freezes — acceptable).
- **Draw:** at the end of `draw_reader` (on top), paint `<glyph> NN%` at
  `content_cols(a) - overlay_w` on row 0. Glyph = `progress_spinner(spin_frame)`
  while `spin_until_ms > now`, else `PROGRESS_REST`. Tinted from the palette
  (dim), so it reads as a quiet HUD element.

## Constants

- `SCROLL_STEP = 1` line per event.
- `FRAME_MS = 80`, `SETTLE_MS = 320` (~4 eased frames — subtle).
- Overlay width ≈ 7 cells (`◟ 100%`).

## Edge cases

- Document shorter than the viewport: `progress_scroll` clamps `top` to 0;
  percent is 100 when everything fits.
- Legend open: overlay anchors to `content_cols`, staying inside the document
  area, left of the panel.
- Collisions: the ~7-cell overlay is drawn last over the far-right of row 0;
  headings rarely reach that column. Accepted.

## Testing

- `tests/progress_test.c`: `progress_percent` at boundaries (line 0 → 0, last →
  100, `nline<=1` → 100, mid values), `progress_scroll` ride-along + clamping
  (top edge, bottom edge, short doc), and `progress_spinner` wrap-around.
- notcurses mouse wiring + settle loop verified by a clean build and a pty smoke
  test (send scroll events, confirm no crash and that it quits cleanly).
- `make test` stays green and ASan/UBSan-clean.

## Docs to update (same change)

- `README.md` — trackpad scrolling + the progress readout in the keys/usage.
- `context.md` — Current status + date stamp.
- `STATUS.md` — module map (`progress.c`) + feature list.
