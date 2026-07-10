/* seed.c — brain scaffolding (see seed.h). The templates below are the
 * generic instance of the memory protocol glance's own memory/ vault runs:
 * an index that doubles as the schema, four small notes, TODO(seed) markers
 * where knowledge belongs — so `glance --doctor` doubles as the fill
 * checklist and "doctor clean" means "brain ready". */
#include "seed.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ---- templates ------------------------------------------------------------ */

static const char MEMORY_TPL[] =
"# Memory\n"
"\n"
"Distilled, curated notes on the state of {repo} — the project's own memory,\n"
"kept as a glance vault: small bounded notes, wikilinked, rewritten in place\n"
"rather than appended to. Anyone (human or agent) picking up work reads this\n"
"index first, then asks the vault instead of browsing it raw:\n"
"`glance --context \"your question\" {vault} --budget 2000`.\n"
"\n"
"- [[status]] — what's on main, what's in flight, what's open\n"
"- [[decisions]] — dated choices, each with the why behind it\n"
"- [[lessons]] — hard-won environment and tooling surprises (earned, never imported)\n"
"- [[history]] — how the project got here, milestone by milestone\n"
"\n"
"## Protocol (distill, don't accumulate)\n"
"\n"
"**Update triggers — if a row fired, write in the same change, not later:**\n"
"\n"
"| You just… | Then… |\n"
"|---|---|\n"
"| shipped / merged / changed behaviour | rewrite the touched lines of [[status]] |\n"
"| settled something a future session could second-guess | dated entry in [[decisions]], with the why |\n"
"| were empirically surprised (a hang, a leak, a wrong assumption) | entry in [[lessons]] — surprises only, never how-tos |\n"
"| landed a milestone, opened or closed a branch | one line in [[history]] |\n"
"| only fixed typos | nothing |\n"
"\n"
"**Delete triggers — stale memory is worse than none:**\n"
"\n"
"- a line the project now contradicts → rewrite it (git history is the archive);\n"
"- a decision that stopped informing anything → drop its entry;\n"
"- a note past ~150 lines → distill it back under.\n"
"\n"
"**Mechanical check — run after any vault edit and fix what it flags:**\n"
"`glance --doctor {vault}` flags oversized (>150 lines) and stale (>45 days)\n"
"notes, orphans, dangling `[[wikilinks]]`, and leftover TODO markers.\n"
"\n"
"**Form:** one `##` per entry — headings are glance's retrieval unit, an entry\n"
"without its own heading is invisible to retrieval; absolute dates only (write\n"
"{date}, never \"today\"); wikilink the notes you mention; `glance --edit` for\n"
"surgical updates.\n";

static const char STATUS_TPL[] =
"# Status\n"
"\n"
"> Last updated: {date} (seeded by `glance --seed`). What's done, what's in\n"
"> flight, what's open. Keep every line true of the present — rewrite, don't\n"
"> append; the why behind load-bearing lines lives in [[decisions]].\n"
"\n"
"## On main\n"
"\n"
"TODO(seed): what {repo} is and what demonstrably works today, as a few\n"
"distilled bullets — read the README and the code, not the aspirations.\n"
"\n"
"## In flight\n"
"\n"
"TODO(seed): work started but not landed — branches, open PRs, half-built\n"
"features. Delete each bullet when it lands (its fact moves to \"On main\").\n"
"\n"
"## Open\n"
"\n"
"TODO(seed): known gaps, rough edges, choices not yet made — the honest\n"
"backlog, not a wishlist.\n";

static const char DECISIONS_TPL[] =
"# Decisions\n"
"\n"
"> Dated entries, newest first, each with the why — this note records why\n"
"> choices exist, so future sessions stop second-guessing them. One `##` per\n"
"> decision; drop an entry once it stops informing anything.\n"
"\n"
"## {date} — This brain was seeded by glance\n"
"\n"
"`glance --seed` scaffolded this vault ([[MEMORY]] holds the index and the\n"
"protocol); an agent filled it from the repo afterwards. The shape follows\n"
"the LLM-wiki pattern: the repo is the immutable source, this vault the\n"
"distilled wiki, the agent instructions the schema.\n"
"\n"
"TODO(seed): mine the repo for the choices that still bind — architecture,\n"
"tooling, conventions, licences — one dated entry each, with the why.\n"
"Sources: git log, the docs, the code's own shape.\n";

static const char LESSONS_TPL[] =
"# Lessons\n"
"\n"
"> Environment and tooling facts that cost real debugging time — check here\n"
"> before chasing a weird hang. One dated `##` per lesson; surprises only,\n"
"> never how-tos.\n"
"\n"
"Seeded empty on purpose: lessons are earned, not imported. When a lesson\n"
"stops being true, delete it — the protocol lives in [[MEMORY]].\n";

static const char HISTORY_TPL[] =
"# History\n"
"\n"
"> The arc of {repo}, milestone by milestone, distilled — one\n"
"> `## YYYY-MM-DD — milestone` per entry, oldest first. Git history is the\n"
"> real append-only log; this note is its distillation.\n"
"\n"
"## {date} — Memory vault seeded\n"
"\n"
"`glance --seed` scaffolded this brain; the protocol lives in [[MEMORY]].\n"
"\n"
"TODO(seed): distill the arc that led here into a few dated entries above\n"
"this one — source: `git log --reverse --date=short --format='%ad %s'`.\n";

static const struct { const char *name; const char *tpl; } NOTES[SEED_NOTES] = {
    { "MEMORY.md",    MEMORY_TPL },
    { "status.md",    STATUS_TPL },
    { "decisions.md", DECISIONS_TPL },
    { "lessons.md",   LESSONS_TPL },
    { "history.md",   HISTORY_TPL },
};

static const char WIRE_TPL[] =
"## Memory\n"
"\n"
"Project state lives in `{vault}/` — a glance-maintained brain (index:\n"
"`{vault}/MEMORY.md`). Before non-trivial work, ask it instead of re-deriving\n"
"the state: `glance --context \"your question\" {vault} --budget 2000`. After\n"
"any non-trivial change, update the vault per the protocol in\n"
"`{vault}/MEMORY.md`, then lint: `glance --doctor {vault}` and fix what it\n"
"flags. Distill, don't append.\n";

static const char DONE_TPL[] =
"glance --doctor {vault} exits 0 (summary.clean true) — the seeded "
"TODO(seed) markers are the fill checklist";

static const SeedStep PLAN[] = {
    { "Fill status.md — the present truth",
      "{vault}/status.md",
      "On main · In flight · Open",
      "README, the code itself, open branches and PRs",
      "glance --edit {vault}/status.md replace \"On main\" \"- ...\" — one "
      "section at a time, replacing each TODO(seed) body with distilled bullets",
      "Only what is true today, with absolute dates; wikilink [[decisions]] "
      "where a line owes its shape to one." },
    { "Distill history.md — how the project got here",
      "{vault}/history.md",
      NULL,
      "git log --reverse --date=short --format='%ad %s'",
      "glance --edit {vault}/history.md before \"{date} — Memory vault seeded\" "
      "\"## YYYY-MM-DD — milestone ...\" — one section per era (3-8 total), in "
      "chronological order so the log stays oldest-first; then rewrite the "
      "seeded entry's body so no TODO(seed) remains",
      "The arc, not a changelog — what a newcomer must know happened." },
    { "Record the decisions that still bind",
      "{vault}/decisions.md",
      NULL,
      "git log, the docs, the code's own shape (architecture, tooling, conventions)",
      "glance --edit {vault}/decisions.md append \"{date} — This brain was "
      "seeded by glance\" \"## YYYY-MM-DD — choice ...\" — one entry per settled "
      "choice with its why, inserting in chronological order (each append lands "
      "directly under the seeded entry, so the newest ends up on top and the "
      "note reads newest-first); then rewrite the seeded entry's body so no "
      "TODO(seed) remains",
      "Only choices a future session could second-guess." },
    { "Wire the brain into the repo's agent instructions",
      "AGENTS.md (and/or CLAUDE.md)",
      NULL,
      "wire_snippet in this output",
      "append wire_snippet to the repo's agent instructions, creating the "
      "file if there is none",
      "The brain only works if every future session is told to read it first." },
    { "Lint until clean",
      "{vault}",
      NULL,
      NULL,
      "glance --doctor {vault} — fix every flag and repeat until it exits 0 "
      "(summary.clean true)",
      "The seeded TODO(seed) markers are the fill checklist; done_when states "
      "the finish line." },
};

/* ---- template expansion ---------------------------------------------------- */

/* Append n bytes of s to the growing buffer; returns 0 on success. */
static int sb_put(char **buf, size_t *len, size_t *cap, const char *s, size_t n) {
    if (*len + n + 1 > *cap) {
        size_t grown = *cap ? *cap * 2 : 256;
        while (grown < *len + n + 1) grown *= 2;
        char *nb = realloc(*buf, grown);
        if (!nb) return 1;
        *buf = nb;
        *cap = grown;
    }
    memcpy(*buf + *len, s, n);
    *len += n;
    (*buf)[*len] = '\0';
    return 0;
}

char *seed_expand(const char *tpl, const char *repo, const char *vault,
                  const char *date) {
    char *buf = NULL;
    size_t len = 0, cap = 0;
    for (const char *p = tpl; *p; ) {
        const char *sub = NULL;
        size_t skip = 0;
        if (*p == '{') {
            if      (!strncmp(p, "{repo}",  6)) { sub = repo;  skip = 6; }
            else if (!strncmp(p, "{vault}", 7)) { sub = vault; skip = 7; }
            else if (!strncmp(p, "{date}",  6)) { sub = date;  skip = 6; }
        }
        if (sub) {
            if (sb_put(&buf, &len, &cap, sub, strlen(sub))) { free(buf); return NULL; }
            p += skip;
        } else {
            if (sb_put(&buf, &len, &cap, p, 1)) { free(buf); return NULL; }
            p++;
        }
    }
    if (!buf) buf = calloc(1, 1);   /* empty template -> empty string */
    return buf;
}

/* ---- repo facts ------------------------------------------------------------ */

typedef struct { char ext[16]; int files; } ExtCount;
typedef struct { ExtCount *v; int n, cap; int seen; } ExtTally;

/* Record one file with extension `ext` (lowercased) in the tally. */
static void tally_add(ExtTally *t, const char *ext) {
    for (int i = 0; i < t->n; i++)
        if (!strcmp(t->v[i].ext, ext)) { t->v[i].files++; return; }
    if (t->n == t->cap) {
        int grown = t->cap ? t->cap * 2 : 16;
        ExtCount *nv = realloc(t->v, (size_t)grown * sizeof *nv);
        if (!nv) return;
        t->v = nv;
        t->cap = grown;
    }
    snprintf(t->v[t->n].ext, sizeof t->v[t->n].ext, "%s", ext);
    t->v[t->n].files = 1;
    t->n++;
}

/* Walk `dir` counting regular files by extension. Dot-entries, symlinks and
 * node_modules are skipped; depth and total file count are capped so a
 * pathological tree cannot stall the seed. */
static void tally_walk(const char *dir, int depth, ExtTally *t) {
    if (depth > 12 || t->seen > 50000) return;
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.' || !strcmp(e->d_name, "node_modules")) continue;
        char path[4096];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        struct stat st;
        if (lstat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) { tally_walk(path, depth + 1, t); continue; }
        if (!S_ISREG(st.st_mode)) continue;
        t->seen++;
        const char *dot = strrchr(e->d_name, '.');
        if (!dot || dot == e->d_name || !dot[1] || strlen(dot + 1) > 15) continue;
        char ext[16];
        int k = 0;
        for (const char *c = dot + 1; *c; c++) ext[k++] = (char)tolower((unsigned char)*c);
        ext[k] = '\0';
        tally_add(t, ext);
    }
    closedir(d);
}

/* Most files first; ties break alphabetically, for a deterministic report. */
static int lang_cmp(const void *a, const void *b) {
    const ExtCount *x = a, *y = b;
    if (x->files != y->files) return y->files - x->files;
    return strcmp(x->ext, y->ext);
}

static int name_cmp(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

int seed_facts(const char *root, SeedFacts *out) {
    memset(out, 0, sizeof *out);

    /* Repo name: the root's base name. */
    const char *base = strrchr(root, '/');
    base = (base && base[1]) ? base + 1 : root;
    snprintf(out->repo, sizeof out->repo, "%s", base);

    /* Top-level entries, sorted; directories carry a trailing '/'. */
    DIR *d = opendir(root);
    if (!d) return 1;
    char **all = NULL;
    int nall = 0, capall = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char path[4096];
        snprintf(path, sizeof path, "%s/%s", root, e->d_name);
        struct stat st;
        if (lstat(path, &st) != 0) continue;
        char entry[300];
        snprintf(entry, sizeof entry, "%s%s", e->d_name,
                 S_ISDIR(st.st_mode) ? "/" : "");
        if (nall == capall) {
            int grown = capall ? capall * 2 : 32;
            char **nv = realloc(all, (size_t)grown * sizeof *nv);
            if (!nv) break;
            all = nv;
            capall = grown;
        }
        all[nall] = strdup(entry);
        if (all[nall]) nall++;
    }
    closedir(d);
    qsort(all, (size_t)nall, sizeof *all, name_cmp);
    for (int i = 0; i < nall; i++) {
        if (i < SEED_MAX_TOP) out->top[out->ntop++] = all[i];
        else free(all[i]);
    }
    free(all);

    /* Dominant file extensions across the whole tree. */
    ExtTally t = {0};
    tally_walk(root, 0, &t);
    qsort(t.v, (size_t)t.n, sizeof *t.v, lang_cmp);
    for (int i = 0; i < t.n && i < SEED_MAX_LANG; i++) {
        snprintf(out->lang[out->nlang].ext, sizeof out->lang[out->nlang].ext,
                 "%s", t.v[i].ext);
        out->lang[out->nlang].files = t.v[i].files;
        out->nlang++;
    }
    free(t.v);

    /* Known entry-point docs the filling agent should read first. */
    static const struct { const char *probe, *shown; int want_dir; } DOCS[] = {
        { "README.md",      "README.md",      0 },
        { "AGENTS.md",      "AGENTS.md",      0 },
        { "CLAUDE.md",      "CLAUDE.md",      0 },
        { "CONTRIBUTING.md","CONTRIBUTING.md",0 },
        { "docs",           "docs/",          1 },
    };
    for (size_t i = 0; i < sizeof DOCS / sizeof DOCS[0]; i++) {
        char path[4096];
        snprintf(path, sizeof path, "%s/%s", root, DOCS[i].probe);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (DOCS[i].want_dir && !S_ISDIR(st.st_mode)) continue;
        if (out->ndocs < SEED_MAX_DOCS) out->docs[out->ndocs++] = DOCS[i].shown;
    }
    return 0;
}

void seed_facts_free(SeedFacts *f) {
    for (int i = 0; i < f->ntop; i++) free(f->top[i]);
    f->ntop = 0;
}

/* ---- notes and plan -------------------------------------------------------- */

void seed_notes(const char *repo, const char *vault, const char *date,
                SeedNote out[SEED_NOTES]) {
    for (int i = 0; i < SEED_NOTES; i++) {
        out[i].name = NOTES[i].name;
        out[i].text = seed_expand(NOTES[i].tpl, repo, vault, date);
    }
}

void seed_notes_free(SeedNote notes[SEED_NOTES]) {
    for (int i = 0; i < SEED_NOTES; i++) {
        free(notes[i].text);
        notes[i].text = NULL;
    }
}

const SeedStep *seed_plan(int *n) {
    *n = (int)(sizeof PLAN / sizeof PLAN[0]);
    return PLAN;
}

const char *seed_wire_snippet(void) { return WIRE_TPL; }

const char *seed_done_when(void) { return DONE_TPL; }
