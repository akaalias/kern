/* platform_stub.c — see platform_stub.h.
 * Definitions of the SDL input-state functions and the kern_x_* Swift bridge
 * that textview.c calls but that have no meaning in a headless test. Because
 * the test binary does not link the SDL runtime or the Swift app, providing
 * these here satisfies the linker and makes the behaviour controllable. */
#include "platform_stub.h"
#include <stdio.h>

static SDL_Keymod g_mod              = KMOD_NONE;
static int        g_x_connected      = 0;
static char       g_x_last_publish[4096];
static int        g_x_has_publish    = 0;
static int        g_x_titlebar       = -1;
static char       g_x_name[128]      = "Test User";
static char       g_x_handle[64]     = "testuser";
static int        g_x_feed_requested = 0;

/* ---- SDL input stubs (no real SDL runtime is linked) ---- */
SDL_Keymod SDL_GetModState(void) { return g_mod; }
void       SDL_StartTextInput(void) {}
void       SDL_StopTextInput(void)  {}

/* ---- X publishing bridge (Swift owns these in the app) ---- */
int  kern_x_is_connected(void) { return g_x_connected; }
void kern_x_publish(const char *text) {
  snprintf(g_x_last_publish, sizeof(g_x_last_publish), "%s", text ? text : "");
  g_x_has_publish = 1;
}
void kern_titlebar_set_x_connected(int connected) { g_x_titlebar = connected; }
void kern_x_fetch_feed(void) { g_x_feed_requested = 1; }
static int g_x_bookmarks_requested = 0;
void kern_x_fetch_bookmarks(void) { g_x_bookmarks_requested = 1; }
int  kern_test_x_bookmarks_requested(void) { return g_x_bookmarks_requested; }

/* Account identity for the tweet-preview overlay (Swift fetches these from
   /2/users/me in the app). No avatar pixels headlessly -> the initials path. */
const char *kern_x_display_name(void) { return g_x_name; }
const char *kern_x_handle(void) { return g_x_handle; }
const unsigned char *kern_x_avatar_rgba(int *w, int *h) {
  if (w) *w = 0; if (h) *h = 0; return 0;
}

/* ---- test controls ---- */
void kern_test_set_modstate(SDL_Keymod mod) { g_mod = mod; }
void kern_test_set_x_connected(int connected) { g_x_connected = connected; }
const char *kern_test_x_last_publish(void) { return g_x_has_publish ? g_x_last_publish : NULL; }
int  kern_test_x_titlebar_state(void) { return g_x_titlebar; }
int  kern_test_x_feed_requested(void) { return g_x_feed_requested; }
void kern_test_set_x_identity(const char *name, const char *handle) {
  snprintf(g_x_name, sizeof(g_x_name), "%s", name ? name : "");
  snprintf(g_x_handle, sizeof(g_x_handle), "%s", handle ? handle : "");
}

void kern_test_platform_reset(void) {
  g_mod = KMOD_NONE;
  g_x_connected = 0;
  g_x_has_publish = 0;
  g_x_last_publish[0] = '\0';
  g_x_titlebar = -1;
  g_x_feed_requested = 0;
  g_x_bookmarks_requested = 0;
  snprintf(g_x_name, sizeof(g_x_name), "Test User");
  snprintf(g_x_handle, sizeof(g_x_handle), "testuser");
}
