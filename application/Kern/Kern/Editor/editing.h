/* editing.h — Text editing operations */
#ifndef EDITING_H
#define EDITING_H

#include "editor_types.h"

/* basic editing */
void ed_insert_char(EditorState *ed, const char *text);
void ed_backspace(EditorState *ed);
void ed_delete(EditorState *ed);
void ed_enter(EditorState *ed);

/* list indentation: add / remove one indent level at the start of the current
   line (used by Tab / Shift-Tab on list items) */
void ed_indent_line(EditorState *ed);
void ed_dedent_line(EditorState *ed);

/* emacs commands */
void ed_emacs_kill_line(EditorState *ed);
void ed_emacs_yank(EditorState *ed);
void ed_emacs_copy_region(EditorState *ed);
void ed_emacs_kill_region(EditorState *ed);
void ed_emacs_kill_word_forward(EditorState *ed);
void ed_emacs_kill_word_backward(EditorState *ed);
void ed_emacs_case_word(EditorState *ed, int mode);
void ed_emacs_transpose_chars(EditorState *ed);
void ed_emacs_forward_word(EditorState *ed);
void ed_emacs_backward_word(EditorState *ed);

#endif
