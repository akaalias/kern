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
