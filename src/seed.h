#ifndef GLANCE_SEED_H
#define GLANCE_SEED_H

/* Brain scaffolding — the deterministic half of `glance --seed`. This module
 * renders the note set of a fresh memory vault (the LLM-wiki shape glance's
 * own memory/ instantiates: an index that doubles as the protocol, plus
 * status / decisions / lessons / history), gathers the orientation facts the
 * filling agent starts from, and states the fill plan. It only builds strings
 * and reads the tree; every write and every judgement lives in agent.c. */

#define SEED_MAX_LANG 6
#define SEED_MAX_TOP  24
#define SEED_MAX_DOCS 8

typedef struct {
    char repo[256];                    /* basename of the repo root */
    struct { char ext[16]; int files; } lang[SEED_MAX_LANG];   /* by count, desc */
    int  nlang;
    char *top[SEED_MAX_TOP];           /* sorted top-level entries, dirs end in '/' */
    int  ntop;
    const char *docs[SEED_MAX_DOCS];   /* known entry-point docs found (static) */
    int  ndocs;
} SeedFacts;

/* Scan the repo at `root` for orientation facts: its name, the dominant file
 * extensions (dot-entries, symlinks and node_modules skipped), the sorted
 * top-level entries, and which known entry-point docs exist. Returns 0 on
 * success, non-zero if root is unreadable. */
int  seed_facts(const char *root, SeedFacts *out);
void seed_facts_free(SeedFacts *f);

#define SEED_NOTES 5

typedef struct {
    const char *name;   /* file name inside the vault (static) */
    char       *text;   /* full note body, owned */
} SeedNote;

/* Render the five notes of a fresh brain for project `repo`, with the vault
 * addressed as `vault` (e.g. "memory") and seeded on `date` (YYYY-MM-DD).
 * Fills out[SEED_NOTES] in write order, index first. */
void seed_notes(const char *repo, const char *vault, const char *date,
                SeedNote out[SEED_NOTES]);
void seed_notes_free(SeedNote notes[SEED_NOTES]);

/* One step of the fill plan. NULL fields do not apply to the step; every
 * string may carry {repo}/{vault}/{date} tokens for seed_expand. */
typedef struct {
    const char *goal, *file, *sections, *sources, *how, *hint;
} SeedStep;

/* The fill plan the calling agent executes, in order. Static storage. */
const SeedStep *seed_plan(int *n);

/* The block the filling agent adds to the host repo's agent instructions
 * (AGENTS.md / CLAUDE.md), and the completion criterion for the whole seed —
 * both templated with {vault}. Static storage. */
const char *seed_wire_snippet(void);
const char *seed_done_when(void);

/* Copy `tpl` with every {repo}, {vault} and {date} token substituted; any
 * other braced text passes through verbatim. Malloc'd; NULL on allocation
 * failure. */
char *seed_expand(const char *tpl, const char *repo, const char *vault,
                  const char *date);

#endif /* GLANCE_SEED_H */
