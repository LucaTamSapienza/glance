#ifndef GLANCE_TUI_H
#define GLANCE_TUI_H

/* Run the Reader-mode TUI on the given Markdown source. Brings up notcurses,
 * renders to a scrollable plane, and handles scroll/quit keys until the user
 * exits. `title` is shown in the status bar (e.g. the file name, or "stdin").
 * Returns 0 on clean exit, non-zero on init failure.
 */
int tui_run(const char *src, unsigned long len, const char *title);

#endif /* GLANCE_TUI_H */
