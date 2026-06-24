/* clipboard_fake.c — in-memory clipboard for the headless tests (implements
 * clipboard.h). Seed with kern_clipboard_set, read back with kern_clipboard_get. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "clipboard.h"

static char g_clip[8192];

void kern_clipboard_set(const char *text) {
  if (!text) { g_clip[0] = '\0'; return; }
  snprintf(g_clip, sizeof g_clip, "%s", text);
}

char *kern_clipboard_get(void) {
  size_t n = strlen(g_clip);
  char *c = malloc(n + 1);
  if (c) memcpy(c, g_clip, n + 1);
  return c;
}

void kern_clipboard_free(char *text) { free(text); }
