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

/* Reply-target info extracted from a note by kern_reply_scan (textview.c):
   the tweet id + author handle come from the status URL itself; author /
   date / quote come from the feed-entry structure above it ("## <name> —
   <date>" heading + "> " blockquote) and are "" when the note doesn't have
   that shape (a hand-written URL still replies, just without the preview). */
typedef struct {
  char id[32];        /* tweet id (from the URL) */
  char handle[64];    /* author @handle (from the URL) */
  char author[128];   /* display name (from the "## " heading) */
  char date[64];      /* heading date, prettified ("Jul 6") when it parses */
  char quote[2048];   /* blockquote text, "> " prefixes stripped */
} KernReplyTarget;

/* Scan `text` for the LAST X status URL followed by non-blank commentary.
   Returns the commentary start (trimmed; *len_out = trimmed length) with
   `t` filled in, or NULL when the note is a plain post. Pure C. */
const char *kern_reply_scan(const char *text, KernReplyTarget *t, int *len_out);

/* Scan `text` for QUOTE shape: plain commentary ABOVE a single feed entry
   (heading + blockquote + status URL) with nothing after the URL. Returns
   the commentary start (trimmed; *len_out = length) with `t` filled in, or
   NULL — text below the URL (that's a reply), another entry above (a
   multi-entry feed note), or no commentary all decline. Pure C. */
const char *kern_quote_scan(const char *text, KernReplyTarget *t, int *len_out);

#ifdef KERN_HEADLESS_TEST
/* Test-only seam: reach the textview.c singletons and reset all mutable
   state (g_ed/g_vs + the modal file-statics) so each test starts clean. */
EditorState *tv_test_ed(void);
ViewState   *tv_test_vs(void);
void         tv_test_reset(void);
/* The recorded "Opened after" predecessor basenames for `path` (see textview.c). */
int          tv_test_opened_after(const char *path, char out[][256], int max);
/* X-publish confirmation overlay state: 0 = closed, 1 = confirming, 2 = sending. */
int          tv_test_pub_state(void);
/* What the pending publish is: 0 = plain post, 1 = reply, 2 = quote. */
int          tv_test_pub_kind(void);
/* The text snapshotted for the pending publish (the tweet-preview body). */
const char  *tv_test_pub_text(void);
/* The reply-target tweet id of the pending publish ("" = a plain post). */
const char  *tv_test_pub_reply_id(void);
/* The quoted-tweet preview fields of the pending reply ("" when absent). */
const char  *tv_test_pub_quote_author(void);
const char  *tv_test_pub_quote_text(void);
const char  *tv_test_pub_reply_handle(void);
#endif

#endif /* EDITOR_LOOP_H */
