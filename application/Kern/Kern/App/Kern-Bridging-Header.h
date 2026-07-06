#ifndef Kern_Bridging_Header_h
#define Kern_Bridging_Header_h

/* Entry point of the C editor (renamed from main() so it doesn't collide with
   SwiftUI's @main). Called by the Swift app on launch. */
int editor_main(int argc, char **argv);

/* Point the editor's file I/O at a directory (the sandbox-container Documents
   folder). Call before editor_main. */
void editor_set_documents_dir(const char *path);

/* Report an X / Twitter publish result in the editor status bar. Implemented in
   C (textview.c); called from the Swift networking layer (KernApp.swift),
   possibly off the main thread. */
void kern_x_set_status(const char *msg);

/* Deliver the async publish result to the confirmation overlay. ok=1 with the
   tweet URL in `info` (copied to the clipboard); ok=0 with an error message.
   Implemented in textview.c; called from the Swift publisher (any thread) and
   applied on the next editor tick. */
void kern_x_publish_done(int ok, const char *info);

/* Deliver the async home-timeline fetch (C-x n). ok=1 with the feed already
   formatted as markdown entries in `text`; ok=0 with an error message.
   Implemented in textview.c; called from the Swift fetcher (any thread) and
   applied on the next editor tick (written to a time-stamped News note). */
void kern_x_feed_done(int ok, const char *text);

/* Deliver the async bookmarks fetch (C-x m) — same contract as
   kern_x_feed_done, landing in a Twitter-Bookmarks note instead. */
void kern_x_bookmarks_done(int ok, const char *text);

/* News-feed noise filter: 1 to leave a post out of the news note (link-only
   posts — e.g. bare image posts — and one-liners). Implemented in textview.c
   (pure string logic, headless-tested); called per post by the Swift feed
   formatter. */
int kern_feed_skip_post(const char *text);

/* Blockquote a post's text into `out`: "> " at the start of every line (">"
   alone on empty lines), truncation-safe and always NUL-terminated. Implemented
   in textview.c (headless-tested); called per post by the Swift formatter. */
void kern_feed_quote_text(const char *text, char *out, int outsz);

/* Open the documents folder in Finder. Implemented in Platform/macos_style.m;
   invoked from the Window menu item (runs on the main thread during tracking). */
void kern_open_documents_folder(void);

/* View toggles for the menu-bar commands. Implemented in textview.c; selected on
   the main thread during menu tracking, the same thread as the render loop. The
   *_enabled() queries report the current on/off state. */
void kern_toggle_syntax(void);
int  kern_syntax_enabled(void);
void kern_toggle_style(void);
int  kern_style_enabled(void);

/* Per-type toggles: one syntax class / style category each (the masters above
   flip everything). The menu's per-item checkmarks read the *_enabled() state. */
void kern_toggle_verbs(void);          int kern_verbs_enabled(void);
void kern_toggle_nouns(void);          int kern_nouns_enabled(void);
void kern_toggle_adjectives(void);     int kern_adjectives_enabled(void);
void kern_toggle_adverbs(void);        int kern_adverbs_enabled(void);
void kern_toggle_function_words(void); int kern_function_words_enabled(void);
void kern_toggle_fillers(void);        int kern_fillers_enabled(void);
void kern_toggle_cliches(void);        int kern_cliches_enabled(void);
void kern_toggle_redundancies(void);   int kern_redundancies_enabled(void);

#endif /* Kern_Bridging_Header_h */
