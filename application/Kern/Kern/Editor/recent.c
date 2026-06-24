/* recent.c — see recent.h. Pure string/data logic, no SDL/renderer. */
#include <stdio.h>
#include <string.h>
#include "recent.h"

static char g_recent[RECENT_MAX][1024];
static int  g_recent_count;

const char *path_base(const char *p) {
  const char *s = strrchr(p, '/');
  return s ? s + 1 : p;
}

void recent_push(const char *path) {
  if (!path || !path[0]) return;
  int top = -1;
  for (int i = 0; i < g_recent_count; i++)
    if (strcmp(g_recent[i], path) == 0) { top = i; break; }
  if (top < 0) top = (g_recent_count < RECENT_MAX) ? g_recent_count++ : RECENT_MAX - 1;
  for (int i = top; i > 0; i--)
    memcpy(g_recent[i], g_recent[i-1], sizeof(g_recent[0]));
  snprintf(g_recent[0], sizeof(g_recent[0]), "%s", path);
}

int recent_count(void) { return g_recent_count; }

const char *recent_get(int i) {
  if (i < 0 || i >= g_recent_count) return NULL;
  return g_recent[i];
}

void recent_reset(void) { g_recent_count = 0; }
