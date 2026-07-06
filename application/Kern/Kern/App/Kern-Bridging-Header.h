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

/* Deliver the async reply-target tweet lookup (kern_x_fetch_tweet): the
   authoritative author/date/text for the confirmation overlay's quoted-post
   preview. ok=0 (or a stale id) is dropped — the note-parsed preview stays.
   Implemented in textview.c; called from Swift (any thread) and applied on
   the next editor tick. */
void kern_x_tweet_done(int ok, const char *id, const char *name,
                       const char *handle, const char *date, const char *text);

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
void kern_toggle_subs(void);           int kern_subs_enabled(void);

/* Menu-bar command bridges (implemented in textview.c). Each drives the exact
   same code path as its keyboard chord — the bridge synthesizes the chord's
   SDL key events — so the menus can never drift from the keyboard. Selected on
   the main thread during menu tracking, the same thread as the render loop. */
/* File */
void kern_menu_open(void);
void kern_menu_save(void);
void kern_menu_save_as(void);
void kern_menu_switch_buffer(void);
void kern_menu_daily_note(void);
/* Edit */
void kern_menu_undo(void);
void kern_menu_cut(void);
void kern_menu_copy(void);
void kern_menu_paste(void);
void kern_menu_select_all(void);
void kern_menu_kill_line(void);
void kern_menu_delete_word_fwd(void);
void kern_menu_delete_word_back(void);
void kern_menu_transpose(void);
void kern_menu_open_line(void);
void kern_menu_upcase(void);
void kern_menu_downcase(void);
void kern_menu_capitalize(void);
void kern_menu_search_fwd(void);
void kern_menu_search_back(void);
/* Format */
void kern_menu_bold(void);
void kern_menu_italic(void);
void kern_menu_highlight(void);
void kern_menu_underline(void);
void kern_menu_code(void);
void kern_menu_sentence_highlight(void);
void kern_menu_sentence_underline(void);
void kern_menu_indent(void);
void kern_menu_outdent(void);
/* View */
void kern_menu_typewriter(void);      int kern_typewriter_enabled(void);
void kern_menu_page_borders(void);    int kern_page_borders_enabled(void);
void kern_menu_font_bigger(void);
void kern_menu_font_smaller(void);
void kern_menu_recenter(void);
void kern_menu_page_down(void);
void kern_menu_page_up(void);
/* Go */
void kern_menu_top(void);
void kern_menu_bottom(void);
void kern_menu_goto_line(void);
void kern_menu_back(void);
void kern_menu_forward(void);
void kern_menu_follow_link(void);
/* Notes */
void kern_menu_extract_note(void);
void kern_menu_margin_note(void);
void kern_menu_fetch_news(void);
void kern_menu_fetch_bookmarks(void);
/* Publish (title-bar button action, reused by the Notes menu) */
void kern_publish_to_x(void);

#endif /* Kern_Bridging_Header_h */
