/* theme.h — color themes for the renderer and the TUI chrome.
 *
 * A Theme is a flat palette of semantic colors. Built-in presets author only
 * their document colors; the chrome colors are derived from polarity + accents
 * (see theme.c). Users can pick a preset by name (config / --theme / live
 * switch) and override any color, or define new named themes, via a small
 * config file. This module is pure (no notcurses, no md4c) and unit tested. */
#ifndef THEME_H
#define THEME_H

#include "render.h"      /* RGB (render.h forward-declares Theme, no cycle) */
#include "highlight.h"   /* HLKind */

typedef struct Theme {
    const char *name;
    int  dark;                        /* polarity: 1 = dark, 0 = light */

    /* document palette (read by render.c) */
    RGB  heading[6];                  /* heading colour by level 1..6 */
    RGB  code_fg, code_bg;
    RGB  link, accent, quote, rule;
    RGB  syntax[HL_OPERATOR + 1];     /* indexed by HLKind; HL_TEXT == code_fg */

    /* UI chrome (read by tui.c), derived from polarity + accents */
    RGB  status_fg, status_bg;
    RGB  sel_fg, sel_bg;
    RGB  hit_fg, hit_bg, hit_focus;
    RGB  panel_bg, panel_fg, panel_border, panel_key, panel_header;
    RGB  divider, progress;
    RGB  cursor_fg, cursor_bg;
} Theme;

/* A built-in or config-defined theme by name, or NULL if unknown. The name
 * "auto" is NOT resolved here (it depends on terminal detection) — callers map
 * "auto" to theme_auto(). */
const Theme *theme_by_name(const char *name);

/* The auto dark or auto light preset (terminal-detected polarity). */
const Theme *theme_auto(int dark);

/* Selectable themes for the live switcher: count and index access (built-ins
 * then any config-defined themes). theme_index_of returns the index of `name`,
 * or 0 if not found. */
int          theme_count(void);
const Theme *theme_at(int index);
int          theme_index_of(const char *name);

/* Parse "#rrggbb" or "rrggbb" into *out. Returns 1 on success, 0 on bad input
 * (out is left unchanged on failure). */
int theme_parse_hex(const char *s, RGB *out);

/* Parse a config buffer: `theme = NAME` sets the default; `[theme:NAME]` with
 * optional `base = PRESET` and `key = #RRGGBB` lines defines/overrides a theme.
 * Unknown keys and bad values are skipped, never fatal. Returns 0. */
int theme_load_config(const char *text);

/* The configured default theme name, or "auto" if none was set. */
const char *theme_default_name(void);

/* The configured keyboard mode from the top-level `keyboard = …` config key:
 * "enhanced" opts the TUI into the kitty keyboard protocol (real modifier bits
 * for Option/Cmd chords on terminals that support it); anything else means the
 * legacy default. Returns "legacy" when the key is absent. */
const char *theme_config_keyboard(void);

/* Produce config text into out (NUL-terminated) with the top-level
 * `theme = <name>` line set: an existing theme line is replaced in place, else
 * one is appended; all other lines (comments, [theme:…] blocks) are preserved.
 * `existing` may be NULL/empty. Returns bytes written, or -1 if it would not fit
 * in cap. Pure: used by the TUI to persist the picked default. */
int theme_config_set_default(const char *existing, const char *name,
                             char *out, size_t cap);

#endif
