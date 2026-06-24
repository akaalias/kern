/* clipboard_sdl.c — SDL-backed clipboard (the app's implementation of
 * clipboard.h). Not compiled into the headless test build. */
#include <SDL2/SDL.h>
#include "clipboard.h"

void kern_clipboard_set(const char *text) {
  if (text) SDL_SetClipboardText(text);
}

char *kern_clipboard_get(void) {
  return SDL_GetClipboardText();   /* SDL returns a malloc'd string ("" if empty) */
}

void kern_clipboard_free(char *text) {
  if (text) SDL_free(text);
}
