/* buffer.h — Document buffer operations */
#ifndef BUFFER_H
#define BUFFER_H

#include "editor_types.h"

/* line operations */
void line_ensure_cap(Line *l, int need);
void line_init(Line *l, const char *s, int len);
void line_dirty(Line *l);
/* Monotonic counter bumped by every line_dirty (i.e. every edit). The render
   loop reads it to tell "the buffer changed this frame" from "the caret merely
   moved" — used by the POS word-in-progress tracker (see pos_render.h). */
unsigned long buf_edit_seq(void);
void buf_ensure_lines_cap(EditorState *ed, int need);
void buf_insert_line_at(EditorState *ed, int idx, const char *s, int len);
void buf_delete_line_at(EditorState *ed, int idx);

/* file I/O — load/save return 0 on success, -1 on failure */
int  buf_load_file(EditorState *ed, const char *path);
void buf_free_all_lines(EditorState *ed);
void buf_init_empty(EditorState *ed);
int  buf_save(EditorState *ed, const char *path);
int  buf_save_text(const char *path, const char *text, int len);
void buf_sanitize_note_title(const char *text, int len, char *out, int outsz);

/* sandboxed file location: all user paths resolve under this directory */
void buf_set_documents_dir(const char *dir);
const char *buf_get_documents_dir(void);
void buf_resolve_path(const char *input, char *out, int outsz);

/* filename completion in the documents dir; out begins with prefix. Returns 1
   if a longer existing match was found, else 0. */
int  buf_complete_filename(const char *prefix, char *out, int outsz);

/* up to `max` filenames in the documents dir starting with `prefix`
   (case-insensitive, alphabetical); returns the count. For wikilink completion. */
int  buf_list_matches(const char *prefix, char out[][256], int max);

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
