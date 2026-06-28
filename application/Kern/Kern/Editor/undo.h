/* undo.h — Operation-based undo system */
#ifndef UNDO_H
#define UNDO_H

#include "editor_types.h"

void undo_push_op(EditorState *ed, UndoOpType type, int line, int col, const char *text, int text_len);
void undo_begin_group(EditorState *ed);
void undo_end_group(EditorState *ed);
void undo_perform(EditorState *ed);  /* undo one step */
void undo_clear(EditorState *ed);    /* drop all history (e.g. on file load) */

#endif
