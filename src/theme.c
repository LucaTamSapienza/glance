/* theme.c — built-in palettes, chrome derivation, and the config parser.
 * Pure: no notcurses, no md4c, no allocation (names live in static pools, so
 * the module is leak-free under ASan). See theme.h. */
#include "theme.h"

#include <string.h>
#include <ctype.h>
#include <stdio.h>

static RGB rgb(uint8_t r, uint8_t g, uint8_t b) { RGB c = {r, g, b}; return c; }

/* ---- chrome derivation ---------------------------------------------------- */

/* Fill the chrome colors from polarity, the document accents, and the code
 * block surface, so panels/status/selection stay cohesive per theme without
 * hand-authoring every field. */
static void derive_chrome(Theme *t) {
    t->syntax[HL_TEXT] = t->code_fg;     /* untokenised code keeps the box colour */
    t->status_bg = t->code_bg; t->status_fg = t->code_fg;
    t->panel_bg  = t->code_bg; t->panel_fg  = t->code_fg;
    t->panel_border = t->rule;
    t->panel_key    = t->accent;
    t->panel_header = t->link;
    t->divider      = t->rule;
    t->progress     = t->quote;
    if (t->dark) {
        t->sel_fg = rgb(245, 245, 245); t->sel_bg = rgb(60, 70, 100);
        t->cursor_fg = rgb(20, 20, 20);  t->cursor_bg = rgb(235, 235, 235);
    } else {
        t->sel_fg = rgb(20, 20, 30);     t->sel_bg = rgb(190, 205, 235);
        t->cursor_fg = rgb(245, 245, 245); t->cursor_bg = rgb(60, 60, 60);
    }
    t->hit_fg = rgb(0, 0, 0); t->hit_bg = rgb(220, 200, 60); t->hit_focus = rgb(255, 170, 40);
}

/* ---- built-in presets ----------------------------------------------------- */

#define NBUILTIN 8
static Theme g_builtin[NBUILTIN];
static int   g_inited = 0;

/* Set a theme's document colours; chrome is derived afterwards. Helper keeps
 * the per-preset blocks compact. syntax order: keyword,type,string,number,
 * comment,function,variable,property,operator. */
static void doc(Theme *t, const char *name, int dark,
                RGB h1, RGB h2, RGB h3, RGB h4, RGB h56,
                RGB code_fg, RGB code_bg, RGB link, RGB accent, RGB quote, RGB rule,
                RGB kw, RGB ty, RGB str, RGB num, RGB com, RGB fn, RGB var, RGB prop, RGB op) {
    t->name = name; t->dark = dark;
    t->heading[0] = h1; t->heading[1] = h2; t->heading[2] = h3;
    t->heading[3] = h4; t->heading[4] = h56; t->heading[5] = h56;
    t->code_fg = code_fg; t->code_bg = code_bg;
    t->link = link; t->accent = accent; t->quote = quote; t->rule = rule;
    t->syntax[HL_KEYWORD] = kw;  t->syntax[HL_TYPE] = ty;     t->syntax[HL_STRING] = str;
    t->syntax[HL_NUMBER] = num;  t->syntax[HL_COMMENT] = com; t->syntax[HL_FUNCTION] = fn;
    t->syntax[HL_VARIABLE] = var; t->syntax[HL_PROPERTY] = prop; t->syntax[HL_OPERATOR] = op;
    derive_chrome(t);
}

static void theme_init(void) {
    if (g_inited) return;
    g_inited = 1;
    Theme *t = g_builtin;

    /* 0: auto-dark — preserves the original dark palette */
    doc(&t[0], "auto-dark", 1,
        rgb(255,135,255), rgb(95,215,255), rgb(135,215,135), rgb(255,215,135), rgb(160,160,160),
        rgb(208,208,208), rgb(48,48,48), rgb(95,175,255), rgb(95,215,255), rgb(120,120,120), rgb(88,88,88),
        rgb(197,134,192), rgb(78,201,176), rgb(152,195,121), rgb(209,154,102), rgb(128,128,128),
        rgb(220,220,170), rgb(224,108,117), rgb(95,175,255), rgb(180,180,180));

    /* 1: auto-light — preserves the original light palette */
    doc(&t[1], "auto-light", 0,
        rgb(175,0,175), rgb(0,135,175), rgb(0,135,0), rgb(175,95,0), rgb(96,96,96),
        rgb(135,0,0), rgb(228,228,228), rgb(0,95,215), rgb(0,135,175), rgb(140,140,140), rgb(170,170,170),
        rgb(175,0,175), rgb(0,135,135), rgb(0,128,0), rgb(170,85,0), rgb(128,128,128),
        rgb(120,90,0), rgb(170,0,0), rgb(0,95,215), rgb(90,90,90));

    /* 2: dracula */
    doc(&t[2], "dracula", 1,
        rgb(255,121,198), rgb(189,147,249), rgb(139,233,253), rgb(80,250,123), rgb(98,114,164),
        rgb(248,248,242), rgb(68,71,90), rgb(139,233,253), rgb(189,147,249), rgb(98,114,164), rgb(98,114,164),
        rgb(255,121,198), rgb(139,233,253), rgb(241,250,140), rgb(189,147,249), rgb(98,114,164),
        rgb(80,250,123), rgb(255,184,108), rgb(139,233,253), rgb(255,121,198));

    /* 3: nord */
    doc(&t[3], "nord", 1,
        rgb(136,192,208), rgb(129,161,193), rgb(143,188,187), rgb(163,190,140), rgb(76,86,106),
        rgb(216,222,233), rgb(59,66,82), rgb(136,192,208), rgb(129,161,193), rgb(97,110,136), rgb(67,76,94),
        rgb(129,161,193), rgb(143,188,187), rgb(163,190,140), rgb(180,142,173), rgb(97,110,136),
        rgb(136,192,208), rgb(208,135,112), rgb(143,188,187), rgb(129,161,193));

    /* 4: gruvbox-dark */
    doc(&t[4], "gruvbox-dark", 1,
        rgb(250,189,47), rgb(184,187,38), rgb(142,192,124), rgb(131,165,152), rgb(146,131,116),
        rgb(235,219,178), rgb(60,56,54), rgb(131,165,152), rgb(254,128,25), rgb(146,131,116), rgb(80,73,69),
        rgb(251,73,52), rgb(250,189,47), rgb(184,187,38), rgb(211,134,155), rgb(146,131,116),
        rgb(184,187,38), rgb(131,165,152), rgb(142,192,124), rgb(254,128,25));

    /* 5: solarized-dark */
    doc(&t[5], "solarized-dark", 1,
        rgb(38,139,210), rgb(42,161,152), rgb(133,153,0), rgb(181,137,0), rgb(88,110,117),
        rgb(131,148,150), rgb(7,54,66), rgb(38,139,210), rgb(42,161,152), rgb(88,110,117), rgb(7,54,66),
        rgb(133,153,0), rgb(181,137,0), rgb(42,161,152), rgb(211,54,130), rgb(88,110,117),
        rgb(38,139,210), rgb(108,113,196), rgb(203,75,22), rgb(131,148,150));

    /* 6: solarized-light */
    doc(&t[6], "solarized-light", 0,
        rgb(38,139,210), rgb(42,161,152), rgb(133,153,0), rgb(181,137,0), rgb(147,161,161),
        rgb(101,123,131), rgb(238,232,213), rgb(38,139,210), rgb(42,161,152), rgb(147,161,161), rgb(238,232,213),
        rgb(133,153,0), rgb(181,137,0), rgb(42,161,152), rgb(211,54,130), rgb(147,161,161),
        rgb(38,139,210), rgb(108,113,196), rgb(203,75,22), rgb(101,123,131));

    /* 7: github-light */
    doc(&t[7], "github-light", 0,
        rgb(5,80,174), rgb(9,105,218), rgb(17,99,41), rgb(149,56,0), rgb(110,119,129),
        rgb(36,41,46), rgb(239,241,243), rgb(9,105,218), rgb(130,80,223), rgb(110,119,129), rgb(208,215,222),
        rgb(207,34,46), rgb(149,56,0), rgb(10,48,105), rgb(5,80,174), rgb(110,119,129),
        rgb(130,80,223), rgb(149,56,0), rgb(17,99,41), rgb(36,41,46));
}

/* ---- config-defined themes ------------------------------------------------ */

#define NCUSTOM 16
static Theme g_custom[NCUSTOM];
static char  g_custom_name[NCUSTOM][64];
static int   g_ncustom = 0;
static char  g_default[64] = "auto";

/* ---- accessors ------------------------------------------------------------ */

const Theme *theme_by_name(const char *name) {
    theme_init();
    if (!name) return NULL;
    for (int i = 0; i < NBUILTIN; i++)
        if (strcmp(g_builtin[i].name, name) == 0) return &g_builtin[i];
    for (int i = 0; i < g_ncustom; i++)
        if (strcmp(g_custom[i].name, name) == 0) return &g_custom[i];
    return NULL;
}

const Theme *theme_auto(int dark) {
    theme_init();
    return dark ? &g_builtin[0] : &g_builtin[1];
}

int theme_count(void) { theme_init(); return NBUILTIN + g_ncustom; }

const Theme *theme_at(int index) {
    theme_init();
    if (index < 0 || index >= NBUILTIN + g_ncustom) return NULL;
    return index < NBUILTIN ? &g_builtin[index] : &g_custom[index - NBUILTIN];
}

int theme_index_of(const char *name) {
    theme_init();
    if (name) {
        for (int i = 0; i < NBUILTIN + g_ncustom; i++) {
            const Theme *t = theme_at(i);
            if (t && strcmp(t->name, name) == 0) return i;
        }
    }
    return 0;
}

const char *theme_default_name(void) { return g_default; }

/* ---- hex parsing ---------------------------------------------------------- */

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int theme_parse_hex(const char *s, RGB *out) {
    if (!s) return 0;
    if (*s == '#') s++;
    int v[6];
    for (int i = 0; i < 6; i++) {
        v[i] = hexval((unsigned char)s[i]);
        if (v[i] < 0) return 0;
    }
    if (s[6] != '\0') return 0;                 /* exactly 6 digits */
    out->r = (uint8_t)(v[0] * 16 + v[1]);
    out->g = (uint8_t)(v[2] * 16 + v[3]);
    out->b = (uint8_t)(v[4] * 16 + v[5]);
    return 1;
}

/* ---- config parsing ------------------------------------------------------- */

/* Assign one color field of t by key name. Unknown keys are ignored. */
static void set_field(Theme *t, const char *key, RGB c) {
    if      (!strcmp(key, "heading1")) t->heading[0] = c;
    else if (!strcmp(key, "heading2")) t->heading[1] = c;
    else if (!strcmp(key, "heading3")) t->heading[2] = c;
    else if (!strcmp(key, "heading4")) t->heading[3] = c;
    else if (!strcmp(key, "heading5")) t->heading[4] = c;
    else if (!strcmp(key, "heading6")) t->heading[5] = c;
    else if (!strcmp(key, "code_fg"))  t->code_fg = c;
    else if (!strcmp(key, "code_bg"))  t->code_bg = c;
    else if (!strcmp(key, "link"))     t->link = c;
    else if (!strcmp(key, "accent"))   t->accent = c;
    else if (!strcmp(key, "quote"))    t->quote = c;
    else if (!strcmp(key, "rule"))     t->rule = c;
    else if (!strcmp(key, "syntax_keyword"))  t->syntax[HL_KEYWORD] = c;
    else if (!strcmp(key, "syntax_type"))     t->syntax[HL_TYPE] = c;
    else if (!strcmp(key, "syntax_string"))   t->syntax[HL_STRING] = c;
    else if (!strcmp(key, "syntax_number"))   t->syntax[HL_NUMBER] = c;
    else if (!strcmp(key, "syntax_comment"))  t->syntax[HL_COMMENT] = c;
    else if (!strcmp(key, "syntax_function")) t->syntax[HL_FUNCTION] = c;
    else if (!strcmp(key, "syntax_variable")) t->syntax[HL_VARIABLE] = c;
    else if (!strcmp(key, "syntax_property")) t->syntax[HL_PROPERTY] = c;
    else if (!strcmp(key, "syntax_operator")) t->syntax[HL_OPERATOR] = c;
    else if (!strcmp(key, "status_fg"))    t->status_fg = c;
    else if (!strcmp(key, "status_bg"))    t->status_bg = c;
    else if (!strcmp(key, "sel_fg"))       t->sel_fg = c;
    else if (!strcmp(key, "sel_bg"))       t->sel_bg = c;
    else if (!strcmp(key, "hit_bg"))       t->hit_bg = c;
    else if (!strcmp(key, "hit_focus"))    t->hit_focus = c;
    else if (!strcmp(key, "panel_bg"))     t->panel_bg = c;
    else if (!strcmp(key, "panel_fg"))     t->panel_fg = c;
    else if (!strcmp(key, "panel_border")) t->panel_border = c;
    else if (!strcmp(key, "panel_key"))    t->panel_key = c;
    else if (!strcmp(key, "panel_header")) t->panel_header = c;
    else if (!strcmp(key, "divider"))      t->divider = c;
    else if (!strcmp(key, "progress"))     t->progress = c;
    else if (!strcmp(key, "cursor_fg"))    t->cursor_fg = c;
    else if (!strcmp(key, "cursor_bg"))    t->cursor_bg = c;
}

/* Trim leading/trailing ASCII whitespace in place; returns the start. */
static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
    return s;
}

int theme_load_config(const char *text) {
    theme_init();
    if (!text) return 0;
    Theme *cur = NULL;                 /* the [theme:NAME] block being defined */
    const char *p = text;
    char line[512];
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) : strlen(p);
        if (n >= sizeof line) n = sizeof line - 1;
        memcpy(line, p, n); line[n] = '\0';
        p = nl ? nl + 1 : p + strlen(p);

        char *s = trim(line);
        if (*s == '\0' || *s == '#') continue;          /* blank / comment line */

        if (*s == '[') {                                 /* [theme:NAME] */
            char *close = strchr(s, ']');
            if (!close) continue;
            *close = '\0';
            char *spec = trim(s + 1);
            if (strncmp(spec, "theme:", 6) != 0) { cur = NULL; continue; }
            char *nm = trim(spec + 6);
            if (*nm == '\0' || g_ncustom >= NCUSTOM) { cur = NULL; continue; }
            cur = &g_custom[g_ncustom];
            *cur = g_builtin[0];                          /* default base: auto-dark */
            snprintf(g_custom_name[g_ncustom], sizeof g_custom_name[0], "%s", nm);
            cur->name = g_custom_name[g_ncustom];
            g_ncustom++;
            continue;
        }

        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = trim(s);
        char *val = trim(eq + 1);

        if (!cur) {
            if (!strcmp(key, "theme"))
                snprintf(g_default, sizeof g_default, "%s", val);
            continue;
        }
        if (!strcmp(key, "base")) {
            const Theme *b = theme_by_name(val);
            if (b) {
                const char *keep = cur->name;             /* preserve our own name */
                *cur = *b;
                cur->name = keep;
            }
            continue;
        }
        RGB c;
        if (theme_parse_hex(val, &c)) set_field(cur, key, c);
    }
    return 0;
}

int theme_config_set_default(const char *existing, const char *name,
                             char *out, size_t cap) {
    if (!name || cap == 0) return -1;
    char line[128];
    int ln = snprintf(line, sizeof line, "theme = %s", name);
    if (ln < 0 || ln >= (int)sizeof line) return -1;

    size_t o = 0;
    int replaced = 0;
#define PUT(s, n) do { if (o + (size_t)(n) >= cap) return -1; memcpy(out + o, (s), (n)); o += (n); } while (0)
#define PUTC(ch)  do { if (o + 1 >= cap) return -1; out[o++] = (ch); } while (0)

    const char *p = existing ? existing : "";
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        int is_theme = 0;
        if (!replaced) {                       /* a top-level `theme = …` line? */
            const char *s = p; size_t l = len;
            while (l && (*s == ' ' || *s == '\t')) { s++; l--; }
            const char *eq = memchr(s, '=', l);
            if (eq && l && *s != '#' && *s != '[') {
                size_t kl = (size_t)(eq - s);
                while (kl && (s[kl - 1] == ' ' || s[kl - 1] == '\t')) kl--;
                if (kl == 5 && strncmp(s, "theme", 5) == 0) is_theme = 1;
            }
        }
        if (is_theme) { PUT(line, ln); replaced = 1; }
        else          { PUT(p, len); }
        PUTC('\n');
        if (!nl) break;
        p = nl + 1;
    }
    if (!replaced) { PUT(line, ln); PUTC('\n'); }
    if (o >= cap) return -1;
    out[o] = '\0';
    return (int)o;
#undef PUT
#undef PUTC
}
