#ifndef MicroEdit_Bridging_Header_h
#define MicroEdit_Bridging_Header_h

/* Entry point of the C editor (renamed from main() so it doesn't collide with
   SwiftUI's @main). Called by the Swift app on launch. */
int editor_main(int argc, char **argv);

#endif /* MicroEdit_Bridging_Header_h */
