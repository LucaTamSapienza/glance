#ifndef GLANCE_COMPLETION_H
#define GLANCE_COMPLETION_H

#include <stdint.h>

/* Minimal bracket auto-pairing for the editor. Typing an opener inserts the
 * matching closer after the cursor; typing a closer that already sits under the
 * cursor steps over it. Backticks/asterisks/underscores are intentionally left
 * alone — Luca prefers to type code fences and emphasis by hand. */

/* Closing char for an opening bracket ('[' -> ']', '(' -> ')', '{' -> '}'),
 * or 0 if `open` is not a paired opener. */
char pair_closer(uint32_t open);

/* True if typing `typed` should skip over an identical closer already at the
 * cursor (so "()" doesn't become "())" when you type the ')'). */
int pair_should_skip(uint32_t typed, char next);

#endif /* GLANCE_COMPLETION_H */
