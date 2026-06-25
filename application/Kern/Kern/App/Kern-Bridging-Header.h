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

/* View toggles for the menu-bar commands. Implemented in textview.c; selected on
   the main thread during menu tracking, the same thread as the render loop. The
   *_enabled() queries report the current on/off state. */
void kern_toggle_syntax(void);
int  kern_syntax_enabled(void);
void kern_toggle_style(void);
int  kern_style_enabled(void);

#endif /* Kern_Bridging_Header_h */
