/* editor_types.h — Shared types, structs, and constants for Kern */

#ifndef EDITOR_TYPES_H
#define EDITOR_TYPES_H

#include <SDL2/SDL.h>

/* ---- constants ---- */
#define TOP_PADDING       82
#define LINE_HEIGHT_MULT  1.5f
#define CHARS_PER_LINE    70
#define MIN_MARGIN        20
#define MAX_UNDO          256
#define MAX_VIS_ROWS      200
#define MAX_LINE_LEN      65536  /* split lines longer than this on load */
#define STATUS_DURATION   3000   /* ms to show a transient status message */
#define TYPEWRITER_FRACTION 0.382f  /* golden ratio: active line pins this far down the page */
#define SCROLL_EASE         0.30f   /* per-frame fraction closed toward scroll_target_y (typewriter glide) */
#define FOCUS_DIM_OPACITY   0.40f   /* opacity of non-focused lines in typewriter mode */
#define FOCUS_EASE          0.30f   /* per-frame fraction of the focus crossfade closed */

/* ---- line ---- */
struct MdSpan;          /* inline-markdown span; defined in md_render.c */
typedef struct {
  char *text;
  int   len;
  int   cap;
  int   wrap_count;             /* cached visual line count, -1 = dirty */
  struct MdSpan *md_spans;      /* cached inline-span map (lazy, owned) */
  int   md_span_count;          /* spans in md_spans; -1 = not yet computed */
} Line;

/* ---- undo (operation-based) ---- */
typedef enum {
  UNDO_NONE = 0,
  UNDO_INSERT,       /* text was inserted at (line, col) */
  UNDO_DELETE,       /* text was deleted from (line, col) */
  UNDO_SPLIT_LINE,   /* line was split at (line, col) */
  UNDO_JOIN_LINE,    /* line+1 was joined into line at col */
  UNDO_GROUP_START,  /* start of compound operation */
  UNDO_GROUP_END,    /* end of compound operation */
} UndoOpType;

typedef struct {
  UndoOpType type;
  int line, col;
  char *text;         /* text that was inserted or deleted (malloc'd) */
  int text_len;
  int cursor_line, cursor_col;  /* cursor position before this op */
} UndoOp;

/* ---- visible row (for post-render markdown pass) ---- */
typedef struct {
  int ln;
  int row_start;
  int row_end;
  int py;
  int heading;
} VisRow;

/* ---- editor state (document model, no SDL dependency) ---- */
typedef struct {
  /* buffer */
  Line *lines;
  int   line_count;
  int   line_cap;
  char  filepath[1024];
  const char *filename;

  /* cursor */
  int cursor_line;
  int cursor_col;
  int cursor_target_col;

  /* mark / region */
  int mark_active;
  int mark_line;
  int mark_col;

  /* kill buffer */
  char *kill_buf;
  int   kill_len;
  int   kill_cap;
  int   last_kill_was_k;

  /* undo */
  UndoOp undo_stack[MAX_UNDO];
  int    undo_top;    /* next write position */
  int    undo_count;  /* total available entries */

  /* unsaved-changes flag (set on any edit, cleared on save) */
  int dirty;
} EditorState;

/* ---- view state (UI, depends on SDL) ---- */
typedef struct ViewState ViewState;
struct ViewState {
  float  scroll_y;
  float  scroll_target_y;   /* typewriter mode eases scroll_y toward this each frame */
  float  font_size;
  int    content_y;
  int    content_h;

  /* page width the cached line wraps are valid for; a reflow is only needed
     when this changes (a resize that keeps the width, or a spurious resize
     event, costs nothing). -1 / 0 forces the first reflow. */
  int    wrap_page_w;

  /* scrollbar */
  int    scrollbar_dragging;
  float  drag_offset;

  /* visible rows cache */
  VisRow vis_rows[MAX_VIS_ROWS];
  int    vis_row_count;
  int    cursor_x;   /* computed during markdown draw, -1 if off-screen */

  /* typewriter mode: pin the active line at a fixed fraction of the page */
  int    typewriter_mode;
  /* focus crossfade: as the caret changes line, the old line fades down and the
     new line fades up over focus_t in [0,1] (1 = settled). */
  int    focus_cur_line;
  int    focus_prev_line;
  float  focus_t;

  /* input mode flags */
  int    ctrl_x_prefix;
  int    esc_prefix;
  int    suppress_next_text;

  /* search */
  int    search_active;
  int    search_direction;   /* 1=forward, -1=backward */
  char   search_buf[256];
  int    search_len;
  int    search_match_line;
  int    search_match_col;

  /* minibuffer */
  int    minibuf_active;
  char   minibuf_prompt[64];
  char   minibuf_text[1024];
  int    minibuf_len;
  void (*minibuf_callback)(const char *text);

  /* status echo area */
  char   status_msg[256];
  Uint32 status_time;
};

#endif /* EDITOR_TYPES_H */
