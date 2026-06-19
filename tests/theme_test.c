/* theme_test.c — unit tests for theme lookup, hex parsing, and config parsing. */
#include "../src/theme.h"

#include <stdio.h>
#include <string.h>

static int fails = 0;
static void expect(int got, int want, const char *msg) {
    if (got != want) { printf("FAIL: %s (got %d, want %d)\n", msg, got, want); fails++; }
}
static void expect_rgb(RGB g, int r, int gg, int b, const char *msg) {
    if (g.r != r || g.g != gg || g.b != b) {
        printf("FAIL: %s (got %d,%d,%d want %d,%d,%d)\n", msg, g.r, g.g, g.b, r, gg, b);
        fails++;
    }
}

int main(void) {
    /* lookup */
    expect(theme_by_name("dracula") != NULL, 1, "dracula exists");
    expect(theme_by_name("nope") == NULL, 1, "unknown theme is NULL");
    expect(theme_by_name("auto") == NULL, 1, "\"auto\" is not a direct name");
    expect(theme_auto(1)->dark, 1, "auto(1) is dark");
    expect(theme_auto(0)->dark, 0, "auto(0) is light");
    expect(theme_count() >= 8, 1, "at least 8 built-ins");
    /* newer built-in palettes are present and dark */
    expect(theme_by_name("tokyo-night") != NULL, 1, "tokyo-night exists");
    expect(theme_by_name("catppuccin-mocha") != NULL, 1, "catppuccin-mocha exists");
    expect(theme_by_name("rose-pine") != NULL, 1, "rose-pine exists");
    expect(theme_by_name("everforest") != NULL, 1, "everforest exists");

    /* HL_TEXT derives from code_fg */
    const Theme *d = theme_by_name("dracula");
    expect_rgb(d->syntax[HL_TEXT], d->code_fg.r, d->code_fg.g, d->code_fg.b,
               "HL_TEXT == code_fg");

    /* hex parsing */
    RGB c;
    expect(theme_parse_hex("#bd93f9", &c), 1, "parse #bd93f9");
    expect_rgb(c, 0xbd, 0x93, 0xf9, "bd93f9 value");
    expect(theme_parse_hex("ff8800", &c), 1, "parse without hash");
    expect_rgb(c, 0xff, 0x88, 0x00, "ff8800 value");
    expect(theme_parse_hex("#fff", &c), 0, "too short rejected");
    expect(theme_parse_hex("#gggggg", &c), 0, "non-hex rejected");
    expect(theme_parse_hex("#1234567", &c), 0, "too long rejected");

    /* config: default name + a custom theme that overrides one color over a base */
    const char *cfg =
        "# my config\n"
        "theme = nord\n"
        "\n"
        "[theme:mine]\n"
        "base = dracula\n"
        "heading1 = #010203\n"
        "bogus_key = #ffffff\n"      /* unknown key: ignored, not fatal */
        "link = nothex\n";           /* bad value: ignored */
    theme_load_config(cfg);

    expect(strcmp(theme_default_name(), "nord"), 0, "default theme = nord");
    const Theme *mine = theme_by_name("mine");
    expect(mine != NULL, 1, "custom theme registered");
    if (mine) {
        expect_rgb(mine->heading[0], 1, 2, 3, "heading1 overridden");
        /* link kept dracula's value (bad override ignored): dracula link = 8be9fd */
        expect_rgb(mine->link, 0x8b, 0xe9, 0xfd, "link kept base (bad value ignored)");
        expect(mine->dark, 1, "custom inherits base polarity (dracula dark)");
    }
    expect(theme_index_of("mine") >= 8, 1, "custom theme indexable for cycling");

    /* persisting the default: set/replace the theme line, preserve the rest */
    char out[512];
    expect(theme_config_set_default("", "nord", out, sizeof out) > 0, 1, "set on empty ok");
    expect(strcmp(out, "theme = nord\n"), 0, "empty -> theme line");

    theme_config_set_default("theme = old\n# keep me\n", "dracula", out, sizeof out);
    expect(strstr(out, "theme = dracula\n") != NULL, 1, "existing theme line replaced");
    expect(strstr(out, "theme = old") == NULL, 1, "old value gone");
    expect(strstr(out, "# keep me") != NULL, 1, "comment preserved");

    const char *with_custom = "# header\n[theme:mine]\nlink = #112233\n";
    theme_config_set_default(with_custom, "gruvbox-dark", out, sizeof out);
    expect(strstr(out, "[theme:mine]") != NULL, 1, "custom block preserved");
    expect(strstr(out, "link = #112233") != NULL, 1, "custom color preserved");
    expect(strstr(out, "theme = gruvbox-dark") != NULL, 1, "theme line appended when absent");

    char tiny[8];
    expect(theme_config_set_default("", "solarized-dark", tiny, sizeof tiny), -1, "overflow -> -1");

    if (fails) { printf("%d theme test(s) FAILED\n", fails); return 1; }
    printf("all theme tests passed\n");
    return 0;
}
