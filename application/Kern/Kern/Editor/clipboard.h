/* clipboard.h — system clipboard access behind a seam.
 *
 * Keeps the editor command layer free of a direct SDL dependency so it can
 * eventually be exercised headlessly. The app links the SDL-backed
 * implementation (clipboard_sdl.c); tests can link a fake. */
#ifndef CLIPBOARD_H
#define CLIPBOARD_H

/* Copy `text` (NUL-terminated) to the system clipboard. */
void kern_clipboard_set(const char *text);

/* Return a heap copy of the clipboard contents (empty string if none, never
   guaranteed NULL by the SDL backend). Free it with kern_clipboard_free. */
char *kern_clipboard_get(void);

/* Release a string returned by kern_clipboard_get. */
void kern_clipboard_free(char *text);

#endif /* CLIPBOARD_H */
