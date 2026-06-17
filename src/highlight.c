/* highlight.c — a generic, spec-driven code highlighter.
 *
 * One tokenizer (hl_line) walks a line and emits styled spans; a per-language
 * LangSpec tells it how comments, strings, keywords and a few quirks (Python
 * triple-strings, shell $vars, YAML/JSON keys) look. Everything an editor would
 * lex with a bespoke grammar is approximated here line-by-line, which is enough
 * for read-only display and keeps each language to a row in a table.
 */
#include "highlight.h"

#include <ctype.h>
#include <string.h>

#define NPOS ((size_t)-1)

/* mode flags on a LangSpec */
#define F_TRIPLE   1u   /* Python """ / ''' triple-quoted strings */
#define F_DOLLAR   2u   /* shell-style $name / ${name} variables */
#define F_KEYCOLON 4u   /* string/ident before ':' is a mapping key */
#define F_FUNC     8u   /* identifier directly before '(' is a call */
#define F_OPS      16u  /* colour operator characters */

typedef struct {
    const char *const *kw;       /* NULL-terminated keyword list */
    const char *const *ty;       /* NULL-terminated type list (optional) */
    const char *line_comment;    /* line-comment marker, or NULL */
    const char *block_open;      /* block-comment opener, or NULL */
    const char *block_close;     /* block-comment closer, or NULL */
    const char *quotes;          /* string delimiters, e.g. "\"'`" */
    unsigned    flags;
} LangSpec;

/* ---- keyword tables ------------------------------------------------------- */

static const char *const kw_c[] = {
    "auto","break","case","const","continue","default","do","else","enum",
    "extern","for","goto","if","inline","register","restrict","return",
    "sizeof","static","struct","switch","typedef","union","volatile","while",
    "signed","unsigned","void","char","short","int","long","float","double",
    "bool","NULL","true","false", NULL };

static const char *const kw_cpp[] = {
    "alignas","auto","break","case","catch","class","const","constexpr",
    "continue","default","delete","do","else","enum","explicit","extern",
    "friend","for","goto","if","inline","namespace","new","noexcept","nullptr",
    "operator","override","private","protected","public","return","sizeof",
    "static","struct","switch","template","this","throw","try","typedef",
    "typename","union","using","virtual","volatile","while","true","false",
    "void","bool","int","char","long","short","float","double","unsigned", NULL };

static const char *const kw_go[] = {
    "break","case","chan","const","continue","default","defer","else",
    "fallthrough","for","func","go","goto","if","import","interface","map",
    "package","range","return","select","struct","switch","type","var",
    "nil","true","false","iota","append","cap","len","make","new","panic",
    "recover","string","int","bool","byte","rune","error", NULL };

static const char *const kw_py[] = {
    "and","as","assert","async","await","break","class","continue","def","del",
    "elif","else","except","finally","for","from","global","if","import","in",
    "is","lambda","nonlocal","not","or","pass","raise","return","try","while",
    "with","yield","None","True","False","self", NULL };

static const char *const kw_js[] = {
    "async","await","break","case","catch","class","const","continue","default",
    "delete","do","else","enum","export","extends","finally","for","from",
    "function","get","if","implements","import","in","instanceof","interface",
    "let","namespace","new","of","private","protected","public","readonly",
    "return","set","static","super","switch","this","throw","try","type",
    "typeof","var","void","while","yield","null","undefined","true","false", NULL };

static const char *const kw_rust[] = {
    "as","async","await","break","const","continue","crate","dyn","else","enum",
    "extern","fn","for","if","impl","in","let","loop","match","mod","move","mut",
    "pub","ref","return","self","static","struct","super","trait","type",
    "unsafe","use","where","while","true","false","Some","None","Ok","Err", NULL };

static const char *const kw_sh[] = {
    "if","then","else","elif","fi","for","while","until","do","done","case",
    "esac","function","in","return","exit","echo","export","local","source",
    "set","unset","read","cd","eval","exec","trap","shift","declare","readonly",
    "printf","test","alias","break","continue", NULL };

static const char *const kw_yaml[] = {
    "true","false","null","yes","no","on","off","True","False","Null","None","~", NULL };

static const char *const kw_json[] = { "true","false","null", NULL };

/* ---- language table ------------------------------------------------------- */
/* Index order must match the L_* enum used by hl_lang. */

enum { L_C, L_CPP, L_GO, L_PY, L_JS, L_RUST, L_BASH, L_YAML, L_JSON, NLANG };

static const LangSpec langs[NLANG] = {
    [L_C]    = { kw_c,    NULL, "//", "/*", "*/", "\"'",  F_FUNC|F_OPS },
    [L_CPP]  = { kw_cpp,  NULL, "//", "/*", "*/", "\"'",  F_FUNC|F_OPS },
    [L_GO]   = { kw_go,   NULL, "//", "/*", "*/", "\"'`", F_FUNC|F_OPS },
    [L_PY]   = { kw_py,   NULL, "#",  NULL, NULL, "\"'",  F_TRIPLE|F_FUNC|F_OPS },
    [L_JS]   = { kw_js,   NULL, "//", "/*", "*/", "\"'`", F_FUNC|F_OPS },
    [L_RUST] = { kw_rust, NULL, "//", "/*", "*/", "\"'",  F_FUNC|F_OPS },
    [L_BASH] = { kw_sh,   NULL, "#",  NULL, NULL, "\"'",  F_DOLLAR|F_OPS },
    [L_YAML] = { kw_yaml, NULL, "#",  NULL, NULL, "\"'",  F_KEYCOLON },
    [L_JSON] = { kw_json, NULL, NULL, NULL, NULL, "\"",   F_KEYCOLON },
};

/* Map a fenced-code info string to a language id (case-insensitive), or -1. */
int hl_lang(const char *name, size_t len) {
    static const struct { const char *alias; int idx; } map[] = {
        {"c",L_C},{"h",L_C},
        {"cpp",L_CPP},{"c++",L_CPP},{"cc",L_CPP},{"cxx",L_CPP},{"hpp",L_CPP},{"hxx",L_CPP},
        {"go",L_GO},{"golang",L_GO},
        {"py",L_PY},{"python",L_PY},{"python3",L_PY},
        {"js",L_JS},{"javascript",L_JS},{"jsx",L_JS},{"mjs",L_JS},{"cjs",L_JS},
        {"ts",L_JS},{"typescript",L_JS},{"tsx",L_JS},
        {"rs",L_RUST},{"rust",L_RUST},
        {"sh",L_BASH},{"bash",L_BASH},{"shell",L_BASH},{"zsh",L_BASH},{"console",L_BASH},
        {"yaml",L_YAML},{"yml",L_YAML},
        {"json",L_JSON},{"jsonc",L_JSON},
    };
    char buf[24];
    if (len == 0 || len >= sizeof buf) return -1;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)name[i];
        buf[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : (char)c;
    }
    buf[len] = '\0';
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
        if (strcmp(buf, map[i].alias) == 0) return map[i].idx;
    return -1;
}

/* ---- small scanners ------------------------------------------------------- */

/* True if `p` occurs at s[i..]. */
static int starts(const char *s, size_t n, size_t i, const char *p) {
    size_t m = strlen(p);
    return i + m <= n && memcmp(s + i, p, m) == 0;
}

/* Index of substring `sub` in s[from..n), or NPOS. */
static size_t find_sub(const char *s, size_t n, size_t from, const char *sub) {
    size_t m = strlen(sub);
    if (m == 0 || m > n) return NPOS;
    for (size_t i = from; i + m <= n; i++)
        if (memcmp(s + i, sub, m) == 0) return i;
    return NPOS;
}

/* Whole-word match of s[i..i+len) against a NULL-terminated list. */
/* True if the word [s, s+len) appears in the NULL-terminated keyword/type list. */
static int in_list(const char *const *list, const char *s, size_t len) {
    if (!list) return 0;
    for (; *list; list++)
        if (strlen(*list) == len && memcmp(*list, s, len) == 0) return 1;
    return 0;
}

/* Identifier character classes: a byte >= 0x80 is treated as part of a word so
 * UTF-8 identifiers stay whole. id_start excludes digits, id_cont includes them. */
static int id_start(unsigned char c) { return c == '_' || isalpha(c) || c >= 0x80; }
static int id_cont(unsigned char c)  { return c == '_' || isalnum(c) || c >= 0x80; }

/* Index of the next non-space/tab byte at or after i. */
static size_t skip_ws(const char *s, size_t n, size_t i) {
    while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
    return i;
}

/* Scan a quoted string starting at the opening quote s[i]; returns the index
 * just past the closing quote (or n if unterminated). Backtick is treated as a
 * raw string with no escape processing. */
static size_t scan_string(const char *s, size_t n, size_t i) {
    char q = s[i];
    int raw = (q == '`');
    size_t j = i + 1;
    while (j < n) {
        if (!raw && s[j] == '\\') { j += (j + 1 < n) ? 2 : 1; continue; }
        if (s[j] == q) { j++; break; }
        j++;
    }
    return j;
}

/* Scan a numeric literal (decimal, hex, float, with `_` separators). */
static size_t scan_number(const char *s, size_t n, size_t i) {
    size_t j = i;
    if (s[j] == '0' && j + 1 < n && (s[j+1] == 'x' || s[j+1] == 'X')) {
        j += 2;
        while (j < n && (isxdigit((unsigned char)s[j]) || s[j] == '_')) j++;
        return j;
    }
    while (j < n) {
        char c = s[j];
        if (isdigit((unsigned char)c) || c == '.' || c == '_') j++;
        else if ((c == 'e' || c == 'E') && j + 1 < n &&
                 (isdigit((unsigned char)s[j+1]) || s[j+1] == '+' || s[j+1] == '-')) j += 2;
        else break;
    }
    return j;
}

/* Scan a shell variable starting at '$': $name, ${...}, or a special like $?. */
static size_t scan_var(const char *s, size_t n, size_t i) {
    size_t j = i + 1;
    if (j >= n) return j;
    if (s[j] == '{') { while (j < n && s[j] != '}') j++; if (j < n) j++; return j; }
    if (strchr("?@#*!$-", s[j])) return j + 1;
    while (j < n && id_cont((unsigned char)s[j])) j++;
    return j;
}

static const char *OPS = "+-*/%=<>!&|^~";

/* ---- the tokenizer -------------------------------------------------------- */

/* Tokenise one code line; see highlight.h. Text not covered by a recognised
 * token is flushed as HL_TEXT so the spans tile the whole line. */
void hl_line(int lang, HLState *st, const char *s, size_t n, HLEmit emit, void *ud) {
    if (lang < 0 || lang >= NLANG) { if (n) emit(ud, HL_TEXT, s, n); return; }
    const LangSpec *L = &langs[lang];
    size_t i = 0, last = 0;

    /* flush pending plain text up to `upto` */
    #define FLUSH(upto) do { if ((upto) > last) emit(ud, HL_TEXT, s + last, (upto) - last); last = (upto); } while (0)
    /* emit a token s[i..end) of `kind`, flushing any text before it */
    #define TOKEN(end, kind) do { FLUSH(i); if ((end) > i) emit(ud, kind, s + i, (end) - i); i = last = (end); } while (0)

    /* continuation of a multi-line block comment */
    if (st->in_block_comment && L->block_close) {
        size_t e = find_sub(s, n, 0, L->block_close);
        if (e == NPOS) { if (n) emit(ud, HL_COMMENT, s, n); return; }
        size_t end = e + strlen(L->block_close);
        emit(ud, HL_COMMENT, s, end);
        st->in_block_comment = 0;
        i = last = end;
    }
    /* continuation of a multi-line triple-quoted string */
    if (st->in_triple) {
        char t[4] = { st->triple_ch, st->triple_ch, st->triple_ch, 0 };
        size_t e = find_sub(s, n, i, t);
        if (e == NPOS) { if (n > i) emit(ud, HL_STRING, s + i, n - i); return; }
        size_t end = e + 3;
        emit(ud, HL_STRING, s + i, end - i);
        st->in_triple = 0;
        i = last = end;
    }

    while (i < n) {
        unsigned char c = (unsigned char)s[i];

        if (L->line_comment && starts(s, n, i, L->line_comment)) { TOKEN(n, HL_COMMENT); break; }

        if (L->block_open && starts(s, n, i, L->block_open)) {
            size_t e = find_sub(s, n, i + strlen(L->block_open), L->block_close);
            if (e == NPOS) { FLUSH(i); emit(ud, HL_COMMENT, s + i, n - i); st->in_block_comment = 1; last = i = n; break; }
            TOKEN(e + strlen(L->block_close), HL_COMMENT);
            continue;
        }

        if ((L->flags & F_TRIPLE) && (c == '"' || c == '\'') &&
            i + 2 < n && s[i+1] == (char)c && s[i+2] == (char)c) {
            char t[4] = { (char)c, (char)c, (char)c, 0 };
            size_t e = find_sub(s, n, i + 3, t);
            if (e == NPOS) { FLUSH(i); emit(ud, HL_STRING, s + i, n - i); st->in_triple = 1; st->triple_ch = (char)c; last = i = n; break; }
            TOKEN(e + 3, HL_STRING);
            continue;
        }

        if (c && strchr(L->quotes, c)) {
            size_t end = scan_string(s, n, i);
            HLKind k = HL_STRING;
            if ((L->flags & F_KEYCOLON)) { size_t p = skip_ws(s, n, end); if (p < n && s[p] == ':') k = HL_PROPERTY; }
            TOKEN(end, k);
            continue;
        }

        if ((L->flags & F_DOLLAR) && c == '$' && i + 1 < n) {
            size_t end = scan_var(s, n, i);
            if (end > i + 1) { TOKEN(end, HL_VARIABLE); continue; }
        }

        if (isdigit(c) || (c == '.' && i + 1 < n && isdigit((unsigned char)s[i+1]))) {
            TOKEN(scan_number(s, n, i), HL_NUMBER);
            continue;
        }

        if (id_start(c)) {
            size_t end = i + 1;
            while (end < n && id_cont((unsigned char)s[end])) end++;
            size_t p = skip_ws(s, n, end);
            HLKind k;
            if ((L->flags & F_KEYCOLON) && p < n && s[p] == ':')        k = HL_PROPERTY;
            else if (in_list(L->kw, s + i, end - i))                    k = HL_KEYWORD;
            else if (in_list(L->ty, s + i, end - i))                    k = HL_TYPE;
            else if ((L->flags & F_FUNC) && p < n && s[p] == '(')       k = HL_FUNCTION;
            else                                                        k = HL_TEXT;
            if (k != HL_TEXT) TOKEN(end, k);
            else i = end;   /* leave as pending plain text */
            continue;
        }

        if ((L->flags & F_OPS) && c && strchr(OPS, c)) {
            size_t end = i + 1;
            while (end < n && s[end] && strchr(OPS, (unsigned char)s[end])) end++;
            TOKEN(end, HL_OPERATOR);
            continue;
        }

        i++;   /* ordinary punctuation / whitespace: pending plain text */
    }
    FLUSH(n);
    #undef FLUSH
    #undef TOKEN
}
