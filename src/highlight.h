#ifndef GLANCE_HIGHLIGHT_H
#define GLANCE_HIGHLIGHT_H

#include <stddef.h>

/* highlight.h — per-language syntax highlighting for fenced code blocks.
 *
 * The highlighter classifies spans of a code line into token kinds; it is
 * colour- and theme-agnostic on purpose, so the renderer owns the palette
 * (kind -> RGB) and the highlighter can be unit-tested on its own. A single
 * generic tokenizer is driven by a per-language spec (keywords, comment and
 * string syntax, a few mode flags), which keeps the whole thing compact. */

/* Token classes the renderer colours. */
typedef enum {
    HL_TEXT,        /* default code colour */
    HL_KEYWORD,     /* language keyword */
    HL_TYPE,        /* built-in type / type-like name */
    HL_STRING,      /* string or character literal */
    HL_NUMBER,      /* numeric literal */
    HL_COMMENT,     /* comment */
    HL_FUNCTION,    /* identifier called as a function: name( */
    HL_VARIABLE,    /* shell-style $variable */
    HL_PROPERTY,    /* mapping key before a colon (YAML / JSON) */
    HL_OPERATOR     /* operator characters */
} HLKind;

/* Context that spans the lines of one code block: block comments and
 * triple-quoted strings continue across line breaks. Zero-initialise before
 * the first line of a block. */
typedef struct {
    int  in_block_comment;
    int  in_triple;     /* inside a Python triple-quoted string */
    char triple_ch;     /* the quote char that opened it (' or ") */
} HLState;

/* Resolve a fenced-code info string (e.g. "python", "sh", "yaml") to an opaque
 * language id, or -1 when the language is unknown (render the code plain). */
int hl_lang(const char *name, size_t len);

/* Called once per contiguous span of a line, in order; the spans concatenate
 * back to the whole line, so the caller can rely on full coverage. */
typedef void (*HLEmit)(void *ud, HLKind kind, const char *s, size_t len);

/* Tokenise one line of code in language `lang`, invoking `emit` for each span.
 * `st` carries block-comment / triple-string context across lines. */
void hl_line(int lang, HLState *st, const char *s, size_t n, HLEmit emit, void *ud);

#endif /* GLANCE_HIGHLIGHT_H */
