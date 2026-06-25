/* navigation.h — Window metrics, word wrapping, cursor navigation, search */
#ifndef NAVIGATION_H
#define NAVIGATION_H

#include "editor_types.h"

/* window / page metrics */
int nav_win_w(void);
int nav_win_h(void);
int nav_page_w(void);
int nav_page_margin(void);
int nav_line_height(void);

/* word wrapping */
int nav_count_wraps(Line *l);
int nav_get_wrap_breaks(Line *l, int *starts, int max_starts);
int nav_total_visual_lines(EditorState *ed);

/* Reflow (invalidate all cached wraps) only if the page width changed since the
   last reflow, recording the new width in vs->wrap_page_w. Returns 1 if it
   reflowed, 0 if the width was unchanged (a no-op). Use on resize, where the
   font is constant so wraps depend solely on the page width. */
int nav_maybe_reflow(EditorState *ed, ViewState *vs);

/* coordinate mapping */
int nav_visual_to_logical(EditorState *ed, int visual_line, int *visual_offset);
int nav_logical_to_visual(EditorState *ed, int logical_line);
int nav_cursor_to_visual(EditorState *ed, int cline, int ccol);

/* cursor */
void nav_cursor_clamp(EditorState *ed);
void nav_ensure_cursor_visible(EditorState *ed, ViewState *vs);

/* The scroll_y that puts the cursor's visual row `fraction` of the way down the
   page (0 = top, 0.5 = center, 0.382 = golden), floored at 0. */
float nav_pin_target(EditorState *ed, ViewState *vs, float fraction);

/* Jump scroll_y (and the ease target) to nav_pin_target — instant, no glide.
   Used by C-l recenter. */
void nav_pin_cursor(EditorState *ed, ViewState *vs, float fraction);

/* click */
void nav_click_to_cursor(EditorState *ed, ViewState *vs, int mx, int my);

/* search */
void nav_search_find_next(EditorState *ed, ViewState *vs, int from_line, int from_col);
void nav_search_find_prev(EditorState *ed, ViewState *vs, int from_line, int from_col);
void nav_search_find_first(EditorState *ed, ViewState *vs);
void nav_search_find_current_dir(EditorState *ed, ViewState *vs);

/* status */
void nav_status_set(ViewState *vs, const char *msg);
const char *nav_status_get(EditorState *ed, ViewState *vs);

#endif
