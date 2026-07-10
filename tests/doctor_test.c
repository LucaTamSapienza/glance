/* doctor_test.c — unit tests for the vault hygiene report, over a temp tree. */
#include "../src/doctor.h"
#include "../src/agent.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static const DoctorNote *find_note(const DoctorReport *r, const char *name) {
    for (int i = 0; i < r->n; i++)
        if (!strcmp(r->v[i].note, name)) return &r->v[i];
    return NULL;
}

/* Capture agent_doctor's stdout into a buffer (the mcp.c capture pattern). */
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

int main(void) {
    char dir[] = "/tmp/glance_doctor_XXXXXX";
    if (!mkdtemp(dir)) { printf("mkdtemp failed\n"); return 1; }
    char p[1024];

    /* a: linked both ways with b, plus a repeated dangling wikilink. */
    snprintf(p, sizeof p, "%s/a.md", dir);
    writef(p, "# A\n\nsee [[b]] and [[ghost]], and [[ghost]] again\n");

    /* b: over the 150-line cap, with a TODO marker, linking back to a. */
    snprintf(p, sizeof p, "%s/b.md", dir);
    FILE *f = fopen(p, "w");
    if (f) {
        fputs("# B\n\nback to [[a]]\n\nTODO: distill this note\n", f);
        for (int i = 0; i < 155; i++) fprintf(f, "filler line %d\n", i);
        fclose(f);
    }

    /* c: no links in or out — an orphan. */
    snprintf(p, sizeof p, "%s/c.md", dir);
    writef(p, "just text, no links\n");

    DoctorReport rep;
    expect(doctor_scan(dir, &rep) == 0, "scan succeeds");
    expect(rep.n == 3, "three notes scanned");

    const DoctorNote *a = find_note(&rep, "a.md");
    const DoctorNote *b = find_note(&rep, "b.md");
    const DoctorNote *c = find_note(&rep, "c.md");
    expect(a && b && c, "all notes present by name");

    if (a) {
        expect(a->outbound == 1, "a: one resolved outbound link");
        expect(a->inbound == 1, "a: one inbound link (from b)");
        expect(a->ndangling == 1, "a: repeated dangling target deduped");
        expect(a->ndangling == 1 && !strcmp(a->dangling[0], "ghost"),
               "a: dangling target is 'ghost'");
        expect(a->todos == 0, "a: no TODO markers");
        expect(a->lines == 3, "a: line count");
        expect(a->mtime > 0, "a: mtime recorded");
    }
    if (b) {
        expect(b->lines > 150, "b: over the line cap");
        expect(b->todos == 1, "b: one TODO marker");
        expect(b->inbound == 1 && b->outbound == 1, "b: linked both ways");
        expect(b->ndangling == 0, "b: no dangling links");
    }
    if (c) {
        expect(c->inbound == 0 && c->outbound == 0, "c: disconnected");
    }
    doctor_free(&rep);
    expect(rep.n == 0 && rep.v == NULL, "free resets the report");

    /* JSON export: flags and summary derived from the same facts. */
    char *json = run_doctor_json(dir);
    expect(json != NULL, "agent_doctor produced output");
    if (json) {
        expect(strstr(json, "\"ok\":true") != NULL, "json: ok");
        expect(strstr(json, "\"oversized\":1") != NULL, "json: one oversized note");
        expect(strstr(json, "\"orphans\":1") != NULL, "json: one orphan");
        expect(strstr(json, "\"dangling\":1") != NULL, "json: one note with dangling links");
        expect(strstr(json, "\"ghost\"") != NULL, "json: dangling target named");
        expect(strstr(json, "\"oversized\"") != NULL, "json: oversized flag emitted");
        expect(strstr(json, "\"orphan\"") != NULL, "json: orphan flag emitted");
        expect(strstr(json, "\"stale\":0") != NULL, "json: nothing stale in a fresh vault");
        free(json);
    }

    /* Unreadable root fails cleanly. */
    DoctorReport bad;
    expect(doctor_scan("/nonexistent-glance-doctor", &bad) != 0, "unreadable root -> error");

    snprintf(p, sizeof p, "%s/a.md", dir); unlink(p);
    snprintf(p, sizeof p, "%s/b.md", dir); unlink(p);
    snprintf(p, sizeof p, "%s/c.md", dir); unlink(p);
    rmdir(dir);

    if (fails) { printf("%d doctor test(s) FAILED\n", fails); return 1; }
    printf("all doctor tests passed\n");
    return 0;
}
