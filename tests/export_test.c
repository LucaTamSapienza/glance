/* export_test.c — unit tests for export's pure logic. The actual HTML->PDF
 * conversion shells out to external tools and is verified manually; the format
 * decision below is what's pure and worth pinning down. */
#include "../src/export.h"

#include <stdio.h>

static int fails = 0;
static void expect(int cond, const char *msg) {
    if (!cond) { printf("FAIL: %s\n", msg); fails++; }
}

int main(void) {
    expect(export_wants_pdf("report.pdf") == 1, "lowercase .pdf -> pdf");
    expect(export_wants_pdf("REPORT.PDF") == 1, "uppercase .PDF -> pdf (case-insensitive)");
    expect(export_wants_pdf("a.pdf") == 1, "short name with .pdf -> pdf");
    expect(export_wants_pdf(".pdf") == 1, "exactly .pdf -> pdf");
    expect(export_wants_pdf("notes.html") == 0, ".html -> not pdf");
    expect(export_wants_pdf("notes") == 0, "no extension -> not pdf");
    expect(export_wants_pdf("pdf") == 0, "too short to hold .pdf -> not pdf");
    expect(export_wants_pdf("my.pdf.bak") == 0, ".pdf not at the end -> not pdf");
    expect(export_wants_pdf(NULL) == 0, "NULL -> not pdf");

    if (fails) { printf("%d export test(s) FAILED\n", fails); return 1; }
    printf("all export tests passed\n");
    return 0;
}
