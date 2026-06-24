/* commands.h — de-globalized editor commands + key dispatch.
 *
 * Commands here operate on explicit EditorState/ViewState pointers (no
 * file-static globals), so they compile into both the app and the headless
 * test binary and can be exercised by feeding key chords to kern_dispatch_key.
 * Commands are migrated here from textview.c's legacy table in batches. */
#ifndef COMMANDS_H
#define COMMANDS_H

#include "editor_types.h"

typedef struct {
  int mod;   /* required modifier: KMOD_CTRL/ALT/GUI, or 0 for none */
  int sym;   /* SDL key symbol */
  void (*action)(EditorState *ed, ViewState *vs);
} Command;

/* Run the command bound to (kmod,sym); returns 1 if one matched, else 0. */
int kern_dispatch_key(EditorState *ed, ViewState *vs, int kmod, int sym);

/* Commands also invoked directly by textview.c's ESC/meta prefix handler. */
void cmd_copy_region(EditorState *ed, ViewState *vs);
void cmd_kill_word_fwd(EditorState *ed, ViewState *vs);
void cmd_upcase_word(EditorState *ed, ViewState *vs);
void cmd_downcase_word(EditorState *ed, ViewState *vs);
void cmd_capitalize_word(EditorState *ed, ViewState *vs);

#endif /* COMMANDS_H */
