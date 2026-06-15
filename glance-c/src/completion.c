/* completion.c — bracket auto-pairing (see completion.h). */
#include "completion.h"

char pair_closer(uint32_t open) {
    switch (open) {
        case '[': return ']';
        case '(': return ')';
        case '{': return '}';
        default:  return 0;
    }
}

int pair_should_skip(uint32_t typed, char next) {
    if ((char)typed != next) return 0;
    return next == ']' || next == ')' || next == '}';
}
