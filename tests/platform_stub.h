/* platform_stub.h — headless replacements for the platform calls textview.c
 * reaches outside the renderer seam: a few SDL input-state functions and the
 * Swift X-publishing bridge. The test binary links these instead of the real
 * SDL runtime / AppKit, and the kern_test_* controls let suites drive and
 * inspect them. */
#ifndef PLATFORM_STUB_H
#define PLATFORM_STUB_H

#include <SDL2/SDL.h>

/* Set the value returned by SDL_GetModState() (drives the text-input modifier
 * guard and any keyup logic). */
void kern_test_set_modstate(SDL_Keymod mod);

/* Set what kern_x_is_connected() reports (gates the publish path + tick sync). */
void kern_test_set_x_connected(int connected);

/* Last text handed to kern_x_publish(), or NULL if none since the last reset. */
const char *kern_test_x_last_publish(void);

/* Last value passed to kern_titlebar_set_x_connected() (-1 = never called). */
int kern_test_x_titlebar_state(void);

/* 1 if kern_x_fetch_feed() has been called since the last reset (C-x n). */
int kern_test_x_feed_requested(void);

/* 1 if kern_x_fetch_bookmarks() has been called since the last reset (C-x m). */
int kern_test_x_bookmarks_requested(void);

/* Set the account identity the tweet-preview overlay reads (display name +
 * @handle). Defaults to "Test User" / "testuser" after a reset. */
void kern_test_set_x_identity(const char *name, const char *handle);

/* Reset all stub state (modstate, connection, recorded publish/titlebar). */
void kern_test_platform_reset(void);

#endif /* PLATFORM_STUB_H */
