/* buffer.h — Document buffer operations */
#ifndef BUFFER_H
#define BUFFER_H

#include "editor_types.h"

/* line operations */
void line_ensure_cap(Line *l, int need);
void line_init(Line *l, const char *s, int len);
void line_dirty(Line *l);
void buf_ensure_lines_cap(EditorState *ed, int need);
void buf_insert_line_at(EditorState *ed, int idx, const char *s, int len);
void buf_delete_line_at(EditorState *ed, int idx);

/* file I/O */
void buf_load_file(EditorState *ed, const char *path);
void buf_free_all_lines(EditorState *ed);
void buf_init_empty(EditorState *ed);
void buf_save(EditorState *ed, const char *path);

/* kill buffer */
void buf_kill_set(EditorState *ed, const char *text, int len);
void buf_kill_append(EditorState *ed, const char *text, int len);

/* mark / region */
void buf_mark_set(EditorState *ed);
void buf_mark_clear(EditorState *ed);
void buf_region_ordered(EditorState *ed, int *sl, int *sc, int *el, int *ec);

/* wrap invalidation */
void buf_invalidate_all_wraps(EditorState *ed);

#endif
