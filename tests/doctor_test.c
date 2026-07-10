/* doctor_test.c — unit tests for the vault hygiene report, over a temp tree. */
#include "../src/doctor.h"
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

static const DoctorNote *find_note(const DoctorReport *r, const char *name) {
    for (int i = 0; i < r->n; i++)
        if (!strcmp(r->v[i].note, name)) return &r->v[i];
    return NULL;
}

/* Capture agent_doctor's stdout into a buffer (the mcp.c capture pattern);
 * *rc gets the export's return code (0 clean, 2 findings, 1 error). */
static char *run_doctor_json(const char *dir, int *rc) {
    fflush(stdout);
    int saved = dup(fileno(stdout));
    FILE *tmp = tmpfile();
    if (!tmp) { close(saved); return NULL; }
    dup2(fileno(tmp), fileno(stdout));
    *rc = agent_doctor(dir, (long)time(NULL));
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
    /* chmod 000 doesn't stop root, so the unreadable case is user-only. */
    int can_lock = geteuid() != 0;

    char dir[] = "/tmp/glance_doctor_XXXXXX";
    if (!mkdtemp(dir)) { printf("mkdtemp failed\n"); return 1; }
    char p[1024];

    /* a: linked both ways with b, plus a repeated dangling wikilink. */
    snprintf(p, sizeof p, "%s/a.md", dir);
    writef(p, "# A\n\nsee [[b]] and [[ghost]], and [[ghost]] again\n");

    /* b: over the 150-line cap; one real marker plus decoys (a fenced code
     * sample and a prose mention); duplicate links to a — one bare, one with
     * a #heading anchor — and a dead relative Markdown link. */
    snprintf(p, sizeof p, "%s/b.md", dir);
    FILE *f = fopen(p, "w");
    if (f) {
        fputs("# B\n\nback to [[a]] and [[a#A]] once more\n\n"
              "TODO: distill this note\n\n"
              "```\nTODO: quoted in a code fence, not a marker\n```\n\n"
              "prose that discusses TODO markers doesn't count either\n\n"
              "a dead [markdown link](missing.md), and safe ones we skip:\n"
              "[web](https://example.com/x.md) and [mail](mailto:a@b.md)\n", f);
        for (int i = 0; i < 150; i++) fprintf(f, "filler line %d\n", i);
        fclose(f);
    }

    /* c: no links in or out — an orphan. Mentions TODO markers in prose,
     * which must not count as a marker (only "TODO:" / "TODO(" do). */
    snprintf(p, sizeof p, "%s/c.md", dir);
    writef(p, "just text, no links, TODO markers only discussed\n");

    /* pic: an image embed whose file exists — not dangling, still an orphan
     * (an attachment is not a note-graph edge). */
    snprintf(p, sizeof p, "%s/pic.md", dir);
    writef(p, "# Pic\n\n![[shot.png]] and [[shot.png]] again\n");
    snprintf(p, sizeof p, "%s/shot.png", dir);
    writef(p, "PNG");

    /* locked: unreadable — reported as such, judged no further. */
    if (can_lock) {
        snprintf(p, sizeof p, "%s/locked.md", dir);
        writef(p, "# Locked\n\nTODO: invisible [[a]]\n");
        chmod(p, 0000);
    }

    DoctorReport rep;
    expect(doctor_scan(dir, &rep) == 0, "scan succeeds");
    expect(rep.n == (can_lock ? 5 : 4), "every note scanned");
    for (int i = 0; i + 1 < rep.n; i++)
        expect(strcmp(rep.v[i].note, rep.v[i+1].note) < 0, "report sorted by path");

    const DoctorNote *a = find_note(&rep, "a.md");
    const DoctorNote *b = find_note(&rep, "b.md");
    const DoctorNote *c = find_note(&rep, "c.md");
    const DoctorNote *pic = find_note(&rep, "pic.md");
    expect(a && b && c && pic, "all notes present by name");

    if (a) {
        expect(a->outbound == 1, "a: one resolved outbound link");
        expect(a->inbound == 1, "a: duplicate inbound links count once");
        expect(a->ndangling == 1, "a: repeated dangling target deduped");
        expect(a->ndangling == 1 && !strcmp(a->dangling[0], "ghost"),
               "a: dangling target is 'ghost'");
        expect(a->todos == 0, "a: no TODO markers");
        expect(a->lines == 3, "a: line count");
        expect(a->mtime > 0, "a: mtime recorded");
    }
    if (b) {
        expect(b->lines > 150, "b: over the line cap");
        expect(b->todos == 1, "b: fence and prose decoys don't count");
        expect(b->inbound == 1 && b->outbound == 1,
               "b: [[a]] + [[a#A]] is one distinct link");
        expect(b->ndangling == 1 && !strcmp(b->dangling[0], "missing.md"),
               "b: the dead relative .md link is dangling; URLs are skipped");
    }
    if (c) {
        expect(c->inbound == 0 && c->outbound == 0, "c: disconnected");
        expect(c->todos == 0, "c: a prose mention of TODO is not a marker");
    }
    if (pic) {
        expect(pic->ndangling == 0, "pic: an existing attachment is not dangling");
        expect(pic->inbound == 0 && pic->outbound == 0, "pic: attachments are not edges");
    }
    if (can_lock) {
        const DoctorNote *locked = find_note(&rep, "locked.md");
        expect(locked && locked->unreadable == 1, "locked: reported unreadable");
        expect(locked && locked->lines == 0 && locked->todos == 0,
               "locked: no facts invented");
    }
    doctor_free(&rep);
    expect(rep.n == 0 && rep.v == NULL, "free resets the report");

    /* JSON export: flags, summary, clean and the exit code from one run. */
    int rc = -1;
    char *json = run_doctor_json(dir, &rc);
    expect(json != NULL, "agent_doctor produced output");
    expect(rc == 2, "findings -> exit 2");
    if (json) {
        expect(strstr(json, "\"ok\":true") != NULL, "json: ok");
        expect(strstr(json, "\"oversized\":1") != NULL, "json: one oversized note");
        expect(strstr(json, "\"orphans\":2") != NULL, "json: two orphans (c, pic)");
        expect(strstr(json, "\"dangling\":2") != NULL, "json: two notes with dangling links");
        expect(strstr(json, "\"todos\":1") != NULL, "json: one note with markers");
        expect(strstr(json, "\"stale\":0") != NULL, "json: nothing stale in a fresh vault");
        expect(strstr(json, "\"clean\":false") != NULL, "json: not clean");
        expect(strstr(json, "\"ghost\"") && strstr(json, "\"missing.md\""),
               "json: dangling targets named");
        expect(strstr(json, "shot.png") == NULL, "json: resolved embed absent");
        expect(strstr(json, "\"oversized\"") && strstr(json, "\"orphan\""),
               "json: flags emitted");
        if (can_lock) {
            expect(strstr(json, "\"unreadable\":1") != NULL, "json: unreadable counted");
            expect(strstr(json, "\"unreadable\"") != NULL, "json: unreadable flag emitted");
        }
        free(json);
    }

    /* A clean vault: report clean, exit 0. */
    char dir2[] = "/tmp/glance_doctor2_XXXXXX";
    if (mkdtemp(dir2)) {
        snprintf(p, sizeof p, "%s/x.md", dir2);
        writef(p, "# X\n\nall quiet\n");
        json = run_doctor_json(dir2, &rc);
        expect(rc == 0, "clean vault -> exit 0");
        expect(json && strstr(json, "\"clean\":true") != NULL, "json: clean:true");
        free(json);
        unlink(p);
        rmdir(dir2);
    }

    /* Unreadable root fails cleanly, with an error body. */
    DoctorReport bad;
    expect(doctor_scan("/nonexistent-glance-doctor", &bad) != 0, "unreadable root -> error");
    json = run_doctor_json("/nonexistent-glance-doctor", &rc);
    expect(rc == 1, "unreadable root -> exit 1");
    expect(json && strstr(json, "\"ok\":false") != NULL, "json: explicit error body");
    free(json);

    if (can_lock) {
        snprintf(p, sizeof p, "%s/locked.md", dir);
        chmod(p, 0644);
        unlink(p);
    }
    snprintf(p, sizeof p, "%s/a.md", dir);     unlink(p);
    snprintf(p, sizeof p, "%s/b.md", dir);     unlink(p);
    snprintf(p, sizeof p, "%s/c.md", dir);     unlink(p);
    snprintf(p, sizeof p, "%s/pic.md", dir);   unlink(p);
    snprintf(p, sizeof p, "%s/shot.png", dir); unlink(p);
    rmdir(dir);

    if (fails) { printf("%d doctor test(s) FAILED\n", fails); return 1; }
    printf("all doctor tests passed\n");
    return 0;
}
