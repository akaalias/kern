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

#endif /* Kern_Bridging_Header_h */
