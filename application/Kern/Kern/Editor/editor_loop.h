#ifndef EDITOR_LOOP_H
#define EDITOR_LOOP_H

/* The pumpable pieces of editor_main's run loop, extracted so the
   event-translation + modal-dispatch layer can be driven headlessly in tests.
   Both compile into the app *and* the test binary; editor_main itself (real
   SDL init + the blocking loop) is app-only (guarded by KERN_HEADLESS_TEST).
   The loop SHAPE is unchanged: wait -> drain via editor_handle_event ->
   editor_tick, so runtime behaviour/snappiness is identical. */

#include <SDL2/SDL.h>
#include "editor_types.h"

/* Translate one SDL event (keydown/textinput/mouse/window) into editor state
   changes. Called once per drained event by the main loop. */
void editor_handle_event(const SDL_Event *ev);

/* Per-frame work: periodic auto-save, X-titlebar connection sync, and the
   render pass. Called once per loop iteration after events are drained. */
void editor_tick(void);

#ifdef KERN_HEADLESS_TEST
/* Test-only seam: reach the textview.c singletons and reset all mutable
   state (g_ed/g_vs + the modal file-statics) so each test starts clean. */
EditorState *tv_test_ed(void);
ViewState   *tv_test_vs(void);
void         tv_test_reset(void);
#endif

#endif /* EDITOR_LOOP_H */
