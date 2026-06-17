/* editor_types.h — Shared types, structs, and constants for MicroEdit */

#ifndef EDITOR_TYPES_H
#define EDITOR_TYPES_H

#include <SDL2/SDL.h>
#include "microui.h"

/* ---- constants ---- */
#define TOP_PADDING       82
#define LINE_HEIGHT_MULT  1.5f
#define CHARS_PER_LINE    70
#define MIN_MARGIN        20
#define MAX_UNDO          256
#define MAX_VIS_ROWS      200
#define STATUS_DURATION   3000   /* ms to show a transient status message */

/* ---- line ---- */
typedef struct {
  char *text;
  int   len;
  int   cap;
  int   wrap_count;   /* cached visual line count, -1 = dirty */
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
} EditorState;

/* ---- view state (UI, depends on SDL/microui) ---- */
typedef struct ViewState ViewState;
struct ViewState {
  float  scroll_y;
  float  font_size;
  int    content_y;
  int    content_h;

  /* scrollbar */
  int    scrollbar_dragging;
  float  drag_offset;

  /* visible rows cache */
  VisRow vis_rows[MAX_VIS_ROWS];
  int    vis_row_count;
  int    cursor_x;   /* computed during markdown draw, -1 if off-screen */

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
  void (*minibuf_callback)(EditorState *ed, ViewState *vs, const char *text);

  /* status echo area */
  char   status_msg[256];
  Uint32 status_time;

  /* microui context */
  mu_Context *ctx;
};

#endif /* EDITOR_TYPES_H */
