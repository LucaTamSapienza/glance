#ifndef GLANCE_DOCTOR_H
#define GLANCE_DOCTOR_H

/* Vault hygiene facts — the mechanical half of a memory protocol. For every
 * note in a vault: how big it is, how long since it was touched, how connected
 * it is, which [[wikilinks]] point nowhere, and whether TODO markers linger.
 * This module only measures; judgements (what counts as oversized or stale)
 * belong to the caller (agent.c applies the documented thresholds). */

typedef struct {
    char  *note;        /* path relative to the vault root, owned */
    int    lines;       /* source lines in the note */
    long   mtime;       /* last modification, Unix seconds */
    int    inbound;     /* distinct notes linking here (self-links excluded) */
    int    outbound;    /* distinct notes this note links to (self-links excluded) */
    char **dangling;    /* link targets that resolve to nothing, owned, deduped */
    int    ndangling;
    int    todos;       /* TODO:/TODO(/FIXME markers left in the text (fences skipped) */
    int    unreadable;  /* the note could not be read; other facts are absent */
} DoctorNote;

typedef struct { DoctorNote *v; int n; } DoctorReport;

/* Scan the vault at `root` and fill `out` (cleared first) with one entry per
 * note, sorted by path. Returns 0 on success, non-zero if `root` is
 * unreadable. */
int doctor_scan(const char *root, DoctorReport *out);

void doctor_free(DoctorReport *out);

#endif /* GLANCE_DOCTOR_H */
