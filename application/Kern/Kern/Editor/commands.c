/* commands.c — de-globalized editor commands + dispatch (see commands.h). */
#include <SDL2/SDL.h>
#include <stdlib.h>
#include <string.h>
#include "commands.h"
#include "navigation.h"
#include "editing.h"
#include "buffer.h"
#include "undo.h"
#include "clipboard.h"

/* ---- cursor movement ---- */

static void cmd_beginning_of_line(EditorState *ed, ViewState *vs) {
  ed->cursor_col = 0;
  ed->cursor_target_col = 0;
  nav_ensure_cursor_visible(ed, vs);
}

static void cmd_end_of_line(EditorState *ed, ViewState *vs) {
  ed->cursor_col = ed->lines[ed->cursor_line].len;
  ed->cursor_target_col = ed->cursor_col;
  nav_ensure_cursor_visible(ed, vs);
}

static void cmd_forward_char(EditorState *ed, ViewState *vs) {
  if (ed->cursor_col < ed->lines[ed->cursor_line].len) {
    ed->cursor_col++;
  } else if (ed->cursor_line < ed->line_count - 1) {
    ed->cursor_line++;
    ed->cursor_col = 0;
  }
  ed->cursor_target_col = ed->cursor_col;
  nav_ensure_cursor_visible(ed, vs);
}

static void cmd_backward_char(EditorState *ed, ViewState *vs) {
  if (ed->cursor_col > 0) {
    ed->cursor_col--;
  } else if (ed->cursor_line > 0) {
    ed->cursor_line--;
    ed->cursor_col = ed->lines[ed->cursor_line].len;
  }
  ed->cursor_target_col = ed->cursor_col;
  nav_ensure_cursor_visible(ed, vs);
}

static void cmd_next_line(EditorState *ed, ViewState *vs) {
  if (ed->cursor_line < ed->line_count - 1) {
    ed->cursor_line++;
    ed->cursor_col = ed->cursor_target_col;
    nav_cursor_clamp(ed);
  }
  nav_ensure_cursor_visible(ed, vs);
}

static void cmd_previous_line(EditorState *ed, ViewState *vs) {
  if (ed->cursor_line > 0) {
    ed->cursor_line--;
    ed->cursor_col = ed->cursor_target_col;
    nav_cursor_clamp(ed);
  }
  nav_ensure_cursor_visible(ed, vs);
}

static void cmd_forward_word(EditorState *ed, ViewState *vs) {
  ed_emacs_forward_word(ed);
  nav_ensure_cursor_visible(ed, vs);
}

static void cmd_backward_word(EditorState *ed, ViewState *vs) {
  ed_emacs_backward_word(ed);
  nav_ensure_cursor_visible(ed, vs);
}

/* ---- editing ---- */

static void cmd_backspace(EditorState *ed, ViewState *vs) {
  buf_mark_clear(ed);
  ed_backspace(ed);
  nav_ensure_cursor_visible(ed, vs);
}

static void cmd_delete(EditorState *ed, ViewState *vs) {
  (void)vs;
  buf_mark_clear(ed);
  ed_delete(ed);
}

static void cmd_enter(EditorState *ed, ViewState *vs) {
  buf_mark_clear(ed);
  ed_enter(ed);
  nav_ensure_cursor_visible(ed, vs);
}

static void cmd_transpose_chars(EditorState *ed, ViewState *vs) {
  ed_emacs_transpose_chars(ed);
  nav_ensure_cursor_visible(ed, vs);
}

static void cmd_open_line(EditorState *ed, ViewState *vs) {   /* C-o */
  int cl = ed->cursor_line, cc = ed->cursor_col;
  buf_mark_clear(ed);
  ed_enter(ed);
  ed->cursor_line = cl;
  ed->cursor_col = cc;
  ed->cursor_target_col = cc;
  nav_ensure_cursor_visible(ed, vs);
}

static void cmd_undo(EditorState *ed, ViewState *vs) {
  buf_mark_clear(ed);
  undo_perform(ed);
  nav_status_set(vs, "Undo");
  nav_ensure_cursor_visible(ed, vs);
}

static void cmd_set_mark(EditorState *ed, ViewState *vs) {
  buf_mark_set(ed);
  nav_status_set(vs, "Mark set");
}

static void cmd_keyboard_quit(EditorState *ed, ViewState *vs) {
  buf_mark_clear(ed);
  nav_status_set(vs, "Quit");
}

/* ---- kill ring / clipboard ---- */

/* mirror the kill buffer to the system clipboard */
static void mirror_kill_to_clipboard(EditorState *ed) {
  if (ed->kill_buf && ed->kill_len > 0) {
    char *tmp = malloc(ed->kill_len + 1);
    if (tmp) {
      memcpy(tmp, ed->kill_buf, ed->kill_len);
      tmp[ed->kill_len] = '\0';
      kern_clipboard_set(tmp);
      free(tmp);
    }
  }
}

static void cmd_kill_line(EditorState *ed, ViewState *vs) {     /* C-k */
  buf_mark_clear(ed);
  ed_emacs_kill_line(ed);
  mirror_kill_to_clipboard(ed);
  nav_ensure_cursor_visible(ed, vs);
}

static void cmd_kill_region(EditorState *ed, ViewState *vs) {   /* C-w */
  ed_emacs_kill_region(ed);
  mirror_kill_to_clipboard(ed);
  nav_status_set(vs, "Region killed");
  nav_ensure_cursor_visible(ed, vs);
}

static void cmd_yank(EditorState *ed, ViewState *vs) {          /* C-y */
  /* prefer the system clipboard so text from other apps can be pasted */
  char *cb = kern_clipboard_get();
  if (cb && cb[0]) buf_kill_set(ed, cb, (int)strlen(cb));
  if (cb) kern_clipboard_free(cb);
  buf_mark_clear(ed);
  ed_emacs_yank(ed);
  nav_status_set(vs, "Yanked");
  nav_ensure_cursor_visible(ed, vs);
}

void cmd_copy_region(EditorState *ed, ViewState *vs) {          /* M-w */
  ed_emacs_copy_region(ed);
  mirror_kill_to_clipboard(ed);
  buf_mark_clear(ed);
  nav_status_set(vs, "Copied region");
  nav_ensure_cursor_visible(ed, vs);
}

void cmd_kill_word_fwd(EditorState *ed, ViewState *vs) {        /* M-d */
  ed_emacs_kill_word_forward(ed);
  mirror_kill_to_clipboard(ed);
  nav_ensure_cursor_visible(ed, vs);
}

static void cmd_kill_word_back(EditorState *ed, ViewState *vs) { /* M-DEL */
  ed_emacs_kill_word_backward(ed);
  mirror_kill_to_clipboard(ed);
  nav_ensure_cursor_visible(ed, vs);
}

void cmd_upcase_word(EditorState *ed, ViewState *vs) {          /* M-u */
  ed_emacs_case_word(ed, 0);
  nav_ensure_cursor_visible(ed, vs);
}

void cmd_downcase_word(EditorState *ed, ViewState *vs) {        /* M-l */
  ed_emacs_case_word(ed, 1);
  nav_ensure_cursor_visible(ed, vs);
}

void cmd_capitalize_word(EditorState *ed, ViewState *vs) {      /* M-c */
  ed_emacs_case_word(ed, 2);
  nav_ensure_cursor_visible(ed, vs);
}

/* ---- dispatch ---- */

static const Command g_commands[] = {
  { KMOD_CTRL, SDLK_a, cmd_beginning_of_line },
  { KMOD_CTRL, SDLK_e, cmd_end_of_line },
  { KMOD_CTRL, SDLK_f, cmd_forward_char },
  { KMOD_CTRL, SDLK_b, cmd_backward_char },
  { KMOD_CTRL, SDLK_n, cmd_next_line },
  { KMOD_CTRL, SDLK_p, cmd_previous_line },
  { KMOD_ALT,  SDLK_f, cmd_forward_word },
  { KMOD_ALT,  SDLK_b, cmd_backward_word },
  { KMOD_CTRL, SDLK_d,      cmd_delete },
  { KMOD_CTRL, SDLK_t,      cmd_transpose_chars },
  { KMOD_CTRL, SDLK_o,      cmd_open_line },
  { KMOD_CTRL, SDLK_SLASH,  cmd_undo },
  { KMOD_CTRL, SDLK_SPACE,  cmd_set_mark },
  { KMOD_CTRL, SDLK_g,      cmd_keyboard_quit },
  { 0, SDLK_BACKSPACE,      cmd_backspace },
  { 0, SDLK_DELETE,         cmd_delete },
  { 0, SDLK_RETURN,         cmd_enter },
  { KMOD_CTRL, SDLK_k,         cmd_kill_line },
  { KMOD_CTRL, SDLK_w,         cmd_kill_region },
  { KMOD_CTRL, SDLK_y,         cmd_yank },
  { KMOD_ALT,  SDLK_w,         cmd_copy_region },
  { KMOD_ALT,  SDLK_d,         cmd_kill_word_fwd },
  { KMOD_ALT,  SDLK_BACKSPACE, cmd_kill_word_back },
  { KMOD_ALT,  SDLK_u,         cmd_upcase_word },
  { KMOD_ALT,  SDLK_l,         cmd_downcase_word },
  { KMOD_ALT,  SDLK_c,         cmd_capitalize_word },
  { 0, 0, NULL },  /* sentinel */
};

static int cmd_matches(const Command *c, int kmod, int sym) {
  if (c->sym != sym) return 0;
  if (c->mod == 0) {
    /* no modifier required — reject if ctrl/alt/gui are held */
    return (kmod & (KMOD_CTRL | KMOD_ALT | KMOD_GUI)) ? 0 : 1;
  }
  return (kmod & c->mod) ? 1 : 0;
}

int kern_dispatch_key(EditorState *ed, ViewState *vs, int kmod, int sym) {
  for (int i = 0; g_commands[i].action; i++) {
    if (cmd_matches(&g_commands[i], kmod, sym)) {
      g_commands[i].action(ed, vs);
      return 1;
    }
  }
  return 0;
}
