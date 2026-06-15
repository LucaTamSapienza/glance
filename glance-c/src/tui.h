#ifndef GLANCE_TUI_H
#define GLANCE_TUI_H

/* Run the TUI on the given Markdown source. Brings up notcurses and loops over
 * the Reader/Insert modes until the user exits. `path` is the file to save to
 * (NULL for stdin, which disables saving); `title` is shown in the status bar.
 * Returns 0 on clean exit, non-zero on init failure.
 */
int tui_run(const char *src, unsigned long len, const char *path, const char *title);

/* Diagnostic mode: print the raw notcurses event (id, utf8 bytes, modifiers)
 * for every key pressed, until Ctrl+Q. Used to see exactly what the terminal
 * sends for keys like Option+3. Returns 0 on clean exit. */
int tui_keyprobe(void);

#endif /* GLANCE_TUI_H */
