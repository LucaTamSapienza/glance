# Streamlined theme selection — design

**Date:** 2026-06-17
**Status:** Implemented (2026-06-17, branch `feature/legend`)
**Scope:** Make choosing a theme easy: a live preview picker, a persisted
default, and a `--list-themes` CLI listing. Builds on the themes feature.

## Goals (chosen in chat)

1. **Live picker with preview** — replaces blind `T`-cycling.
2. **Persist the choice** — the picked theme becomes the default next launch,
   no hand-editing.
3. **`glance --list-themes`** — print the available theme names.

(Not doing: a `:theme <name>` command.)

## Design

**1. Picker (`T` opens it; left panel like the TOC)**
- `App` gains `int themepick`, `int theme_sel`, `const Theme *theme_saved`.
- `T` (reader) opens the picker: `theme_saved = a->theme`,
  `theme_sel = theme_index_of(a->theme->name)`, `themepick = 1`.
- `handle_themepick`: `j`/`k` (and arrows, `g`/`G`) move the selection and
  **apply it live** — set `a->theme = theme_at(theme_sel)`, sync `a->dark`,
  `rerender`. `Enter` commits (persists, see #2, closes). `Esc`/`q`/`T` revert
  (`a->theme = theme_saved`, rerender) and close.
- `draw_themepick`: a `TOC_W`-wide left panel listing every theme
  (`theme_count`/`theme_at`), the selection highlighted, the current default
  marked, and a footer hint `↑↓ move · ⏎ keep · esc revert`.
- Dispatch routes `themepick` before `handle_reader`, like `tocmode`.

**2. Persist the choice**
- Pure transform in `theme.c` (unit-tested):
  `int theme_config_set_default(const char *existing, const char *name, char
  *out, size_t cap)` — emit the config text with the top-level `theme = <name>`
  line replaced (or appended if absent), preserving all other lines (custom
  `[theme:…]` blocks, comments). Returns bytes written, or -1 if it would not
  fit.
- `tui.c` `save_default_theme(name)`: read `~/.config/glance/config` (empty if
  absent) → transform → `mkdir -p ~/.config/glance` → `atomic_write`. Called on
  picker commit. Failure is non-fatal (status message).

**3. `glance --list-themes`**
- `main.c` subcommand: read the same config (so custom themes show), call
  `theme_load_config`, then print each `theme_at(i)->name` one per line; exit 0.

## Testing

`theme_test.c` gains cases for `theme_config_set_default`: empty input →
`theme = X`; existing `theme =` line replaced; comments + a `[theme:…]` block
preserved when absent (line appended); overflow returns -1. Picker/persist
wiring verified by build + `--list-themes` output. `make test` stays green and
ASan/UBSan-clean.

## Docs

`README.md` (T = picker, `--list-themes`), `STATUS.md` (keys), `context.md`.
