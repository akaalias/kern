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

/* ---- test controls ---- */
void kern_test_set_modstate(SDL_Keymod mod) { g_mod = mod; }
void kern_test_set_x_connected(int connected) { g_x_connected = connected; }
const char *kern_test_x_last_publish(void) { return g_x_has_publish ? g_x_last_publish : NULL; }
int  kern_test_x_titlebar_state(void) { return g_x_titlebar; }

void kern_test_platform_reset(void) {
  g_mod = KMOD_NONE;
  g_x_connected = 0;
  g_x_has_publish = 0;
  g_x_last_publish[0] = '\0';
  g_x_titlebar = -1;
}
