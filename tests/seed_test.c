/* seed_test.c — unit tests for the brain scaffold, over a temp repo tree. */
#include "../src/seed.h"
#include "../src/agent.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int fails = 0;
static void expect(int cond, const char *msg) {
    if (!cond) { printf("FAIL: %s\n", msg); fails++; }
}

static void writef(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    if (f) { fputs(content, f); fclose(f); }
}

/* Count the occurrences of `needle` in `s`. */
static int count_occ(const char *s, const char *needle) {
    int n = 0;
    size_t step = strlen(needle);
    for (const char *p = s; (p = strstr(p, needle)) != NULL; p += step) n++;
    return n;
}

/* Capture agent_seed's stdout into a buffer (the mcp.c capture pattern);
 * *rc gets the export's return code. */
static char *run_seed_json(const char *dir, int *rc) {
    fflush(stdout);
    int saved = dup(fileno(stdout));
    FILE *tmp = tmpfile();
    if (!tmp) { close(saved); return NULL; }
    dup2(fileno(tmp), fileno(stdout));
    *rc = agent_seed(dir, (long)time(NULL));
    fflush(stdout);
    dup2(saved, fileno(stdout));
    close(saved);
    long sz = ftell(tmp);
    rewind(tmp);
    char *out = calloc(1, (size_t)sz + 1);
    if (out) fread(out, 1, (size_t)sz, tmp);
    fclose(tmp);
    return out;
}

/* Same capture for agent_doctor — seeding and linting close one loop. */
static char *run_doctor_json(const char *dir) {
    fflush(stdout);
    int saved = dup(fileno(stdout));
    FILE *tmp = tmpfile();
    if (!tmp) { close(saved); return NULL; }
    dup2(fileno(tmp), fileno(stdout));
    agent_doctor(dir, (long)time(NULL));
    fflush(stdout);
    dup2(saved, fileno(stdout));
    close(saved);
    long sz = ftell(tmp);
    rewind(tmp);
    char *out = calloc(1, (size_t)sz + 1);
    if (out) fread(out, 1, (size_t)sz, tmp);
    fclose(tmp);
    return out;
}

static const SeedNote *find_note(const SeedNote *notes, const char *name) {
    for (int i = 0; i < SEED_NOTES; i++)
        if (!strcmp(notes[i].name, name)) return &notes[i];
    return NULL;
}

int main(void) {
    /* ---- seed_expand: token substitution ---- */
    char *x = seed_expand("a {repo} b {vault} c {date} {repo}", "R", "V", "D");
    expect(x && !strcmp(x, "a R b V c D R"), "expand replaces every token");
    free(x);
    x = seed_expand("plain {unknown} braces", "R", "V", "D");
    expect(x && !strcmp(x, "plain {unknown} braces"), "unknown braces pass through");
    free(x);
    x = seed_expand("", "R", "V", "D");
    expect(x && !strcmp(x, ""), "empty template -> empty string");
    free(x);

    /* ---- temp repo: .git marker, README, docs/, two .c files ---- */
    char dir[] = "/tmp/glance_seed_XXXXXX";
    if (!mkdtemp(dir)) { printf("mkdtemp failed\n"); return 1; }
    char p[1024];
    snprintf(p, sizeof p, "%s/.git", dir); mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/docs", dir); mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/src", dir);  mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/README.md", dir);  writef(p, "# Demo\n");
    snprintf(p, sizeof p, "%s/src/a.c", dir);    writef(p, "int a;\n");
    snprintf(p, sizeof p, "%s/src/b.c", dir);    writef(p, "int b;\n");
    snprintf(p, sizeof p, "%s/src/b.h", dir);    writef(p, "int b;\n");

    /* ---- facts ---- */
    SeedFacts f;
    expect(seed_facts(dir, &f) == 0, "facts scan succeeds");
    expect(strstr(f.repo, "glance_seed_") != NULL, "repo named from the root");
    expect(f.nlang == 3, "three extensions tallied");
    expect(f.nlang == 3 && !strcmp(f.lang[0].ext, "c") && f.lang[0].files == 2,
           "dominant extension first");
    expect(f.nlang == 3 && !strcmp(f.lang[1].ext, "h") && !strcmp(f.lang[2].ext, "md"),
           "extension ties break alphabetically");
    expect(f.ntop == 3, "three top-level entries (dot-entries skipped)");
    expect(f.ntop == 3 && !strcmp(f.top[0], "README.md") &&
           !strcmp(f.top[1], "docs/") && !strcmp(f.top[2], "src/"),
           "top level sorted, directories marked");
    expect(f.ndocs == 2 && !strcmp(f.docs[0], "README.md") && !strcmp(f.docs[1], "docs/"),
           "entry-point docs detected");
    seed_facts_free(&f);
    expect(seed_facts("/nonexistent-glance-seed", &f) != 0, "unreadable root -> error");

    /* ---- note rendering ---- */
    SeedNote notes[SEED_NOTES];
    seed_notes("demo", "memory", "2026-07-10", notes);
    const SeedNote *mem = find_note(notes, "MEMORY.md");
    const SeedNote *sta = find_note(notes, "status.md");
    const SeedNote *dec = find_note(notes, "decisions.md");
    const SeedNote *les = find_note(notes, "lessons.md");
    const SeedNote *his = find_note(notes, "history.md");
    expect(mem && sta && dec && les && his, "all five notes rendered");
    if (mem && mem->text) {
        expect(strstr(mem->text, "state of demo") != NULL, "index names the repo");
        expect(strstr(mem->text, "[[status]]") && strstr(mem->text, "[[decisions]]") &&
               strstr(mem->text, "[[lessons]]") && strstr(mem->text, "[[history]]"),
               "index wikilinks every note");
        expect(strstr(mem->text, "| only fixed typos | nothing |") != NULL,
               "index carries the trigger table");
        expect(strstr(mem->text, "glance --doctor memory") != NULL,
               "protocol names the mechanical check");
        expect(count_occ(mem->text, "TODO(") == 0, "index has no fill markers");
    }
    if (sta && sta->text) {
        expect(count_occ(sta->text, "TODO(seed):") == 3, "status: one marker per section");
        expect(strstr(sta->text, "Last updated: 2026-07-10") != NULL, "status dated");
    }
    if (dec && dec->text)
        expect(count_occ(dec->text, "TODO(seed):") == 1, "decisions: one marker");
    if (his && his->text) {
        expect(count_occ(his->text, "TODO(seed):") == 1, "history: one marker");
        expect(strstr(his->text, "## 2026-07-10 — Memory vault seeded") != NULL,
               "history opens with the seed milestone");
    }
    if (les && les->text)
        expect(count_occ(les->text, "TODO(") == 0, "lessons seeded empty on purpose");
    seed_notes_free(notes);
    expect(notes[0].text == NULL, "free resets the note set");

    /* ---- the plan and its companions ---- */
    int nsteps = 0;
    const SeedStep *plan = seed_plan(&nsteps);
    expect(nsteps == 5, "five plan steps");
    expect(nsteps == 5 && plan[0].sections && strstr(plan[0].sections, "On main") != NULL,
           "step 1 names the status sections");
    expect(nsteps == 5 && plan[4].how && strstr(plan[4].how, "--doctor") != NULL,
           "last step is the lint loop");
    expect(strstr(seed_wire_snippet(), "{vault}/MEMORY.md") != NULL,
           "wire snippet points at the index");
    expect(strstr(seed_done_when(), "todos:0") != NULL, "done-check is doctor-clean");

    /* ---- agent_seed end-to-end: scaffold on disk + JSON bundle ---- */
    int rc = -1;
    char *json = run_seed_json(dir, &rc);
    expect(rc == 0 && json != NULL, "seed succeeds");
    if (json) {
        expect(strstr(json, "\"ok\":true") != NULL, "json: ok");
        expect(strstr(json, "\"marker\":\".git\"") != NULL, "json: marker found");
        expect(strstr(json, "\"created\":[\"memory/MEMORY.md\",\"memory/status.md\","
                            "\"memory/decisions.md\",\"memory/lessons.md\","
                            "\"memory/history.md\"]") != NULL,
               "json: all five notes created, index first");
        expect(strstr(json, "\"skipped\":[]") != NULL, "json: nothing skipped");
        expect(strstr(json, "\"ext\":\"c\",\"files\":2") != NULL, "json: facts carried");
        expect(strstr(json, "\"plan\":[{\"step\":1") != NULL, "json: plan present");
        expect(strstr(json, "\"wire_snippet\":\"## Memory") != NULL,
               "json: wire snippet expanded");
        expect(strstr(json, "{vault}") == NULL, "json: no unexpanded tokens");
        free(json);
    }
    snprintf(p, sizeof p, "%s/memory/MEMORY.md", dir);
    struct stat st;
    expect(stat(p, &st) == 0, "index written to disk");

    /* ---- idempotence: a second run touches nothing ---- */
    json = run_seed_json(dir, &rc);
    expect(rc == 0 && json != NULL, "re-seed succeeds");
    if (json) {
        expect(strstr(json, "\"created\":[]") != NULL, "re-seed creates nothing");
        expect(count_occ(json, "memory/") >= 5, "re-seed reports the existing notes");
        free(json);
    }

    /* ---- additive: only the missing note is recreated ---- */
    snprintf(p, sizeof p, "%s/memory/status.md", dir);
    unlink(p);
    json = run_seed_json(dir, &rc);
    expect(rc == 0 && json != NULL, "partial re-seed succeeds");
    if (json) {
        expect(strstr(json, "\"created\":[\"memory/status.md\"]") != NULL,
               "only the missing note is recreated");
        free(json);
    }

    /* ---- the loop closes: doctor over a fresh seed flags exactly the fill ---- */
    char vault[1024];
    snprintf(vault, sizeof vault, "%s/memory", dir);
    char *doc = run_doctor_json(vault);
    expect(doc != NULL, "doctor runs over the seeded vault");
    if (doc) {
        expect(strstr(doc, "\"todos\":3") != NULL,
               "doctor: exactly the three notes with fill markers");
        expect(strstr(doc, "\"oversized\":0") && strstr(doc, "\"stale\":0") &&
               strstr(doc, "\"orphans\":0") && strstr(doc, "\"dangling\":0"),
               "doctor: nothing else flagged on a fresh seed");
        free(doc);
    }

    /* ---- unreadable dir fails cleanly ---- */
    json = run_seed_json("/nonexistent-glance-seed", &rc);
    expect(rc != 0, "unreadable dir -> error");
    free(json);

    /* ---- cleanup ---- */
    static const char *vault_files[] =
        { "MEMORY.md", "status.md", "decisions.md", "lessons.md", "history.md" };
    for (size_t i = 0; i < 5; i++) {
        snprintf(p, sizeof p, "%s/memory/%s", dir, vault_files[i]);
        unlink(p);
    }
    snprintf(p, sizeof p, "%s/memory", dir);    rmdir(p);
    snprintf(p, sizeof p, "%s/README.md", dir); unlink(p);
    snprintf(p, sizeof p, "%s/src/a.c", dir);   unlink(p);
    snprintf(p, sizeof p, "%s/src/b.c", dir);   unlink(p);
    snprintf(p, sizeof p, "%s/src/b.h", dir);   unlink(p);
    snprintf(p, sizeof p, "%s/src", dir);       rmdir(p);
    snprintf(p, sizeof p, "%s/docs", dir);      rmdir(p);
    snprintf(p, sizeof p, "%s/.git", dir);      rmdir(p);
    rmdir(dir);

    if (fails) { printf("%d seed test(s) FAILED\n", fails); return 1; }
    printf("all seed tests passed\n");
    return 0;
}
