/* md_render.h — Markdown-aware text rendering */
#ifndef MD_RENDER_H
#define MD_RENDER_H

#include "editor_types.h"
#include "microui.h"

/* check if a logical line is a list item, return indent in pixels (0 if not) */
int md_list_indent(Line *l);

/* pixel width of a list item's marker ("- " / "12. "), 0 if not a list item */
int md_list_marker_width(Line *l);

/* check if a logical line is a heading (starts with # ) */
int md_is_heading(Line *l);

/* length of a heading's "### " prefix (hashes + the single space), or 0 if not
   a heading. The prefix is rendered hanging in the right margin, not in flow. */
int md_heading_prefix_len(Line *l);

/* draw a text segment with markdown inline formatting.
   text is the full line text, start..end is the range to draw.
   x,y is the starting position. heading = whether this line is a heading.
   track_cursor_col: if >= 0, record x position when we reach this column
   (written to *out_cursor_x, or -1 if not found).
   returns the x position after drawing. */
float md_draw_text(const char *text, int start, int end,
                   float x, float y, mu_Color base_color, int heading,
                   int track_cursor_col, int *out_cursor_x);

#endif
