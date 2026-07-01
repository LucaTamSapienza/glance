# Themes — design

**Date:** 2026-06-17
**Status:** Implemented (2026-06-17, branch `feature/legend`)
**Scope:** Curated built-in color themes + user config overrides, selectable by
config default, `--theme` flag, and a live in-app switcher. Approach **A** (a
`Theme` threaded through), agreed over a global.

## Built-in themes

`auto` (default — terminal-detected dark/light), `dracula`, `nord`,
`gruvbox-dark`, `solarized-dark`, `solarized-light`, `github-light`.

## Architecture

**New module `src/theme.{c,h}` (CORE — no notcurses/md4c, unit-tested):**

```c
typedef struct Theme {
    const char *name;
    int  dark;                       /* polarity */
    RGB  heading[6];
    RGB  code_fg, code_bg, link, accent, quote, rule;
    RGB  syntax[HL_OPERATOR + 1];    /* by HLKind; HL_TEXT == code_fg */
    /* chrome (derived from polarity + accents): */
    RGB  status_fg, status_bg, sel_fg, sel_bg, hit_fg, hit_bg, hit_focus,
         panel_bg, panel_fg, panel_border, panel_key, panel_header,
         divider, progress, cursor_fg, cursor_bg;
} Theme;
```

- Built-ins are filled once by `theme_init()`: each authors only its ~21
  **document** colors; `derive_chrome()` fills the chrome from polarity + accents
  so the panels/status/selection stay cohesive without per-theme hand-tuning.
- API: `theme_by_name(name)`, `theme_auto(int dark)`, `theme_count()`,
  `theme_at(i)`, `theme_index_of(name)` (for cycling), `theme_default_name()`,
  `theme_parse_hex(s, *RGB)`, `theme_load_config(const char *text)`.
- **Config** (`~/.config/glance/config`, tiny dependency-free format):
  `theme = nord` sets the default; `[theme:myname]` + optional `base = dracula`
  + `key = #RRGGBB` lines define/override a theme. Keys map by name
  (`heading1..6`, `code_fg`, `link`, `accent`, `syntax_string`, `status_bg`, …).
  Bad hex / unknown keys are skipped, never fatal. Parser takes a text buffer so
  it is unit-testable.

**`render.c`:** the palette helpers (`heading_fg`, `code_bg`, …, `hl_fg`,
`border_style`) read from a `const Theme *` held in the renderer state instead of
an `int dark`. New entry `render_doc_themed(src, len, width, const Theme*,
basedir)`; `render_doc`/`render_doc_at` stay as thin shims mapping their `int
dark` to `theme_auto(dark)` — so `agent.c` and every test call site are
unchanged.

**Header cycle:** `render.h` keeps `RGB` and forward-declares `typedef struct
Theme Theme;`; `theme.h` includes `render.h` (for `RGB`) + `highlight.h` (for
`HLKind`). No include cycle.

**`tui.c`:** `App` gains `const Theme *theme` (and keeps `dark` in sync with
`theme->dark` for any niche color not yet themed). Loads config at startup;
`--theme <name>` / `-l` override; **`T`** cycles `theme_at()` live — re-renders
the Doc with the new palette and flashes the name in the status bar (session
only; config is the persistent default). Reader chrome (status bar, legend, TOC,
backlinks, selection, search hits, divider, progress HUD, block cursor) reads
the theme; niche graph/image colors may stay on `dark` for v1.

**`main_render.c` / `main.c`:** add `--theme <name>`; `-l` becomes a shortcut for
the auto-light theme.

## Scope calls (agreed)

- **Page background stays the terminal default** — themes set foregrounds +
  element backgrounds (code, panels, status, selection), not a full-screen paint.
- **Non-goals (v1):** no truecolor/256 fallback negotiation, no per-document
  themes, no writing the live choice back to config.

## Testing

`tests/theme_test.c`: `theme_by_name` hit/miss, `theme_auto` polarity,
`theme_parse_hex` (valid / no-hash / bad / clamp), and a config-buffer parse
asserting the default name, a color override, and a `[theme:NAME]` custom theme.
Existing render/doc_ansi/search/toc/agent tests pass a default theme via the
unchanged `int dark` shims. `make test` stays green and ASan/UBSan-clean.

## Docs to update

`README.md` (themes + `--theme` + `T`), `context.md`, `STATUS.md` (module map +
features).
