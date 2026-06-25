/* md_render.h — Markdown-aware text rendering */
#ifndef MD_RENDER_H
#define MD_RENDER_H

#include "editor_types.h"
#include "gfx.h"

/* check if a logical line is a list item, return indent in pixels (0 if not) */
int md_list_indent(Line *l);

/* pixel width of a list item's marker ("- " / "12. "), 0 if not a list item */
int md_list_marker_width(Line *l);

/* whether a line is a list item ("- " or "N. "), ignoring any leading
   indentation whitespace — true for nested items like "  - foo" too */
int md_is_list_item(Line *l);

/* check if a logical line is a heading (starts with # ) */
int md_is_heading(Line *l);

/* length of a heading's "### " prefix (hashes + the single space), or 0 if not
   a heading. The prefix is rendered hanging in the right margin, not in flow. */
int md_heading_prefix_len(Line *l);

/* draw a line's visual-row window [start,end) with markdown inline formatting,
   using the line's cached inline-span map (so formatting carries across wrapped
   rows). x,y is the starting position; heading = whether the line is a heading.
   track_cursor_col: if in [start,end], record x position at that column
   (written to *out_cursor_x). Returns the x position after drawing. */
float md_draw_text(Line *l, int start, int end,
                   float x, float y, Color base_color, int heading,
                   int track_cursor_col, int *out_cursor_x, int draw);

/* x position (px) where column `col` renders, using the same per-span font
   metrics as md_draw_text — for aligning the selection/search highlight. */
int md_col_x(Line *l, int start, int end, int x0, int heading, int col);

/* Set the global focus-dim opacity applied to all subsequent md_draw_text output
   (1.0 = full). Typewriter mode fades non-focused lines; reset to 1.0 after. */
void md_set_text_opacity(float o);

/* Opacity for a line mid focus-crossfade: the focused line `cur` fades up from
   FOCUS_DIM_OPACITY→1, the line just left `prev` fades 1→dim, others stay dim.
   t in [0,1] is the crossfade progress (1 = settled). */
float md_focus_opacity(int line, int cur, int prev, float t);

#endif
