#ifndef MicroEdit_Bridging_Header_h
#define MicroEdit_Bridging_Header_h

/* Entry point of the C editor (renamed from main() so it doesn't collide with
   SwiftUI's @main). Called by the Swift app on launch. */
int editor_main(int argc, char **argv);

/* Point the editor's file I/O at a directory (the sandbox-container Documents
   folder). Call before editor_main. */
void editor_set_documents_dir(const char *path);

#endif /* MicroEdit_Bridging_Header_h */
