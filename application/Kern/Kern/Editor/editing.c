/* editing.c — Text editing operations */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "editing.h"
#include "buffer.h"
#include "undo.h"

/* ---- basic editing ---- */

void ed_insert_char(EditorState *ed, const char *text) {
  int tlen = strlen(text);
  int ins_line = ed->cursor_line;
  int ins_col = ed->cursor_col;
  Line *l = &ed->lines[ed->cursor_line];
  line_ensure_cap(l, l->len + tlen);
  memmove(l->text + ed->cursor_col + tlen, l->text + ed->cursor_col, l->len - ed->cursor_col + 1);
  memcpy(l->text + ed->cursor_col, text, tlen);
  l->len += tlen;
  ed->cursor_col += tlen;
  ed->cursor_target_col = ed->cursor_col;
  line_dirty(l);
  undo_push_op(ed, UNDO_INSERT, ins_line, ins_col, text, tlen);
}

void ed_backspace(EditorState *ed) {
  if (ed->cursor_col > 0) {
    Line *l = &ed->lines[ed->cursor_line];
    char deleted = l->text[ed->cursor_col - 1];
    undo_push_op(ed, UNDO_DELETE, ed->cursor_line, ed->cursor_col - 1, &deleted, 1);
    memmove(l->text + ed->cursor_col - 1, l->text + ed->cursor_col, l->len - ed->cursor_col + 1);
    l->len--;
    ed->cursor_col--;
    line_dirty(l);
  } else if (ed->cursor_line > 0) {
    int prev = ed->cursor_line - 1;
    int new_col = ed->lines[prev].len;
    undo_push_op(ed, UNDO_JOIN_LINE, prev, new_col, NULL, 0);
    line_ensure_cap(&ed->lines[prev], ed->lines[prev].len + ed->lines[ed->cursor_line].len);
    memcpy(ed->lines[prev].text + ed->lines[prev].len, ed->lines[ed->cursor_line].text, ed->lines[ed->cursor_line].len + 1);
    ed->lines[prev].len += ed->lines[ed->cursor_line].len;
    buf_delete_line_at(ed, ed->cursor_line);
    ed->cursor_line = prev;
    ed->cursor_col = new_col;
    line_dirty(&ed->lines[prev]);
  }
  ed->cursor_target_col = ed->cursor_col;
}

void ed_delete(EditorState *ed) {
  Line *l = &ed->lines[ed->cursor_line];
  if (ed->cursor_col < l->len) {
    char deleted = l->text[ed->cursor_col];
    undo_push_op(ed, UNDO_DELETE, ed->cursor_line, ed->cursor_col, &deleted, 1);
    memmove(l->text + ed->cursor_col, l->text + ed->cursor_col + 1, l->len - ed->cursor_col);
    l->len--;
    line_dirty(l);
  } else if (ed->cursor_line < ed->line_count - 1) {
    int next = ed->cursor_line + 1;
    undo_push_op(ed, UNDO_JOIN_LINE, ed->cursor_line, l->len, NULL, 0);
    line_ensure_cap(l, l->len + ed->lines[next].len);
    memcpy(l->text + l->len, ed->lines[next].text, ed->lines[next].len + 1);
    l->len += ed->lines[next].len;
    buf_delete_line_at(ed, next);
    line_dirty(l);
  }
}

void ed_enter(EditorState *ed) {
  Line *l = &ed->lines[ed->cursor_line];

  /* detect list prefix on current line */
  char prefix[32] = "";
  int prefix_len = 0;
  if (l->len >= 2 && l->text[0] == '-' && l->text[1] == ' ') {
    prefix[0] = '-'; prefix[1] = ' '; prefix_len = 2;
  } else {
    /* check for "N. " numbered list */
    int ni = 0;
    while (ni < l->len && l->text[ni] >= '0' && l->text[ni] <= '9') ni++;
    if (ni > 0 && ni + 1 < l->len && l->text[ni] == '.' && l->text[ni+1] == ' ') {
      /* parse the number and increment */
      int num = 0;
      for (int j = 0; j < ni; j++) num = num * 10 + (l->text[j] - '0');
      prefix_len = snprintf(prefix, sizeof(prefix), "%d. ", num + 1);
    }
  }

  /* push undo ops (grouped if there's a list prefix) */
  if (prefix_len > 0) undo_begin_group(ed);
  undo_push_op(ed, UNDO_SPLIT_LINE, ed->cursor_line, ed->cursor_col, NULL, 0);
  if (prefix_len > 0) {
    undo_push_op(ed, UNDO_INSERT, ed->cursor_line + 1, 0, prefix, prefix_len);
    undo_end_group(ed);
  }

  int rest_len = l->len - ed->cursor_col;
  /* build new line: prefix + rest of current line */
  int new_len = prefix_len + rest_len;
  char *new_text = malloc(new_len + 1);
  memcpy(new_text, prefix, prefix_len);
  memcpy(new_text + prefix_len, l->text + ed->cursor_col, rest_len);
  new_text[new_len] = '\0';

  buf_insert_line_at(ed, ed->cursor_line + 1, new_text, new_len);
  free(new_text);

  l = &ed->lines[ed->cursor_line];
  l->len = ed->cursor_col;
  l->text[l->len] = '\0';
  line_dirty(l);
  ed->cursor_line++;
  ed->cursor_col = prefix_len;
  ed->cursor_target_col = ed->cursor_col;
}

/* ---- emacs commands ---- */

void ed_emacs_kill_line(EditorState *ed) {
  Line *l = &ed->lines[ed->cursor_line];
  undo_begin_group(ed);
  if (ed->cursor_col < l->len) {
    int kill_count = l->len - ed->cursor_col;
    undo_push_op(ed, UNDO_DELETE, ed->cursor_line, ed->cursor_col, l->text + ed->cursor_col, kill_count);
    if (ed->last_kill_was_k) {
      buf_kill_append(ed, l->text + ed->cursor_col, kill_count);
    } else {
      buf_kill_set(ed, l->text + ed->cursor_col, kill_count);
    }
    l->len = ed->cursor_col;
    l->text[l->len] = '\0';
    line_dirty(l);
  } else if (ed->cursor_line < ed->line_count - 1) {
    undo_push_op(ed, UNDO_JOIN_LINE, ed->cursor_line, ed->cursor_col, NULL, 0);
    if (ed->last_kill_was_k) {
      buf_kill_append(ed, "\n", 1);
    } else {
      buf_kill_set(ed, "\n", 1);
    }
    Line *next = &ed->lines[ed->cursor_line + 1];
    line_ensure_cap(l, l->len + next->len);
    memcpy(l->text + l->len, next->text, next->len + 1);
    l->len += next->len;
    buf_delete_line_at(ed, ed->cursor_line + 1);
    line_dirty(l);
  }
  undo_end_group(ed);
  ed->last_kill_was_k = 1;
}

void ed_emacs_yank(EditorState *ed) {
  if (!ed->kill_buf || ed->kill_len == 0) return;
  undo_begin_group(ed);
  for (int i = 0; i < ed->kill_len; i++) {
    if (ed->kill_buf[i] == '\n') {
      ed_enter(ed);
    } else {
      char ch[2] = { ed->kill_buf[i], 0 };
      ed_insert_char(ed, ch);
    }
  }
  undo_end_group(ed);
}

/* M-w (kill-ring-save): copy the region into the kill buffer without deleting. */
void ed_emacs_copy_region(EditorState *ed) {
  if (!ed->mark_active) return;
  int sl, sc, el, ec;
  buf_region_ordered(ed, &sl, &sc, &el, &ec);

  int total = 0;
  for (int ln = sl; ln <= el; ln++) {
    int cs = (ln == sl) ? sc : 0;
    int ce = (ln == el) ? ec : ed->lines[ln].len;
    total += ce - cs;
    if (ln < el) total++;
  }
  char *region = malloc(total + 1);
  if (!region) return;
  int pos = 0;
  for (int ln = sl; ln <= el; ln++) {
    int cs = (ln == sl) ? sc : 0;
    int ce = (ln == el) ? ec : ed->lines[ln].len;
    memcpy(region + pos, ed->lines[ln].text + cs, ce - cs);
    pos += ce - cs;
    if (ln < el) region[pos++] = '\n';
  }
  region[pos] = '\0';
  buf_kill_set(ed, region, total);
  free(region);
}

/* Kill (cut to kill buffer, with undo) the ordered range [sl,sc] .. [el,ec].
   Leaves the cursor at the start of the range. Mark is left untouched. */
static void ed_kill_range(EditorState *ed, int sl, int sc, int el, int ec) {
  int total = 0;
  for (int ln = sl; ln <= el; ln++) {
    int cs = (ln == sl) ? sc : 0;
    int ce = (ln == el) ? ec : ed->lines[ln].len;
    total += ce - cs;
    if (ln < el) total++;
  }
  if (total <= 0) {
    ed->cursor_line = sl; ed->cursor_col = sc; ed->cursor_target_col = sc;
    return;
  }
  char *region = malloc(total + 1);
  if (!region) return;
  int pos = 0;
  for (int ln = sl; ln <= el; ln++) {
    int cs = (ln == sl) ? sc : 0;
    int ce = (ln == el) ? ec : ed->lines[ln].len;
    memcpy(region + pos, ed->lines[ln].text + cs, ce - cs);
    pos += ce - cs;
    if (ln < el) region[pos++] = '\n';
  }
  region[pos] = '\0';
  buf_kill_set(ed, region, total);

  undo_begin_group(ed);
  undo_push_op(ed, UNDO_DELETE, sl, sc, region, total);
  undo_end_group(ed);
  free(region);

  ed->cursor_line = sl; ed->cursor_col = sc;
  if (sl == el) {
    Line *l = &ed->lines[sl];
    memmove(l->text + sc, l->text + ec, l->len - ec + 1);
    l->len -= (ec - sc);
    line_dirty(l);
  } else {
    Line *first = &ed->lines[sl];
    Line *last  = &ed->lines[el];
    int new_len = sc + (last->len - ec);
    line_ensure_cap(first, new_len);
    memmove(first->text + sc, last->text + ec, last->len - ec + 1);
    first->len = new_len;
    line_dirty(first);
    for (int i = el; i > sl; i--) buf_delete_line_at(ed, i);
  }
  if (ed->cursor_line < 0) ed->cursor_line = 0;
  if (ed->cursor_line >= ed->line_count) ed->cursor_line = ed->line_count - 1;
  if (ed->cursor_col < 0) ed->cursor_col = 0;
  if (ed->cursor_col > ed->lines[ed->cursor_line].len) ed->cursor_col = ed->lines[ed->cursor_line].len;
  ed->cursor_target_col = ed->cursor_col;
}

void ed_emacs_kill_region(EditorState *ed) {
  if (!ed->mark_active) return;
  int sl, sc, el, ec;
  buf_region_ordered(ed, &sl, &sc, &el, &ec);
  ed_kill_range(ed, sl, sc, el, ec);
  buf_mark_clear(ed);
}

void ed_emacs_forward_word(EditorState *ed) {
  Line *l = &ed->lines[ed->cursor_line];
  while (ed->cursor_col < l->len && l->text[ed->cursor_col] != ' ') ed->cursor_col++;
  while (ed->cursor_col < l->len && l->text[ed->cursor_col] == ' ') ed->cursor_col++;
  if (ed->cursor_col >= l->len && ed->cursor_line < ed->line_count - 1) {
    ed->cursor_line++; ed->cursor_col = 0;
  }
  ed->cursor_target_col = ed->cursor_col;
}

void ed_emacs_backward_word(EditorState *ed) {
  if (ed->cursor_col == 0 && ed->cursor_line > 0) {
    ed->cursor_line--;
    ed->cursor_col = ed->lines[ed->cursor_line].len;
  }
  Line *l = &ed->lines[ed->cursor_line];
  while (ed->cursor_col > 0 && l->text[ed->cursor_col - 1] == ' ') ed->cursor_col--;
  while (ed->cursor_col > 0 && l->text[ed->cursor_col - 1] != ' ') ed->cursor_col--;
  ed->cursor_target_col = ed->cursor_col;
}

/* M-d: kill from point to the end of the next word. */
void ed_emacs_kill_word_forward(EditorState *ed) {
  int sl = ed->cursor_line, sc = ed->cursor_col;
  ed_emacs_forward_word(ed);
  int el = ed->cursor_line, ec = ed->cursor_col;
  if (el == sl && ec == sc) return;
  ed_kill_range(ed, sl, sc, el, ec);
}

/* M-DEL: kill from the start of the previous word to point. */
void ed_emacs_kill_word_backward(EditorState *ed) {
  int el = ed->cursor_line, ec = ed->cursor_col;
  ed_emacs_backward_word(ed);
  int sl = ed->cursor_line, sc = ed->cursor_col;
  if (sl == el && sc == ec) return;
  ed_kill_range(ed, sl, sc, el, ec);
}

/* M-u / M-l / M-c: change the case of the word at/after point (mode 0=upper,
   1=lower, 2=capitalize), leaving point after the word. Single line. */
void ed_emacs_case_word(EditorState *ed, int mode) {
  Line *l = &ed->lines[ed->cursor_line];
  while (ed->cursor_col < l->len && l->text[ed->cursor_col] == ' ') ed->cursor_col++;
  int start = ed->cursor_col;
  int end = start;
  while (end < l->len && l->text[end] != ' ') end++;
  int wlen = end - start;
  if (wlen <= 0) { ed->cursor_target_col = ed->cursor_col; return; }

  char *xf = malloc(wlen);
  if (!xf) return;
  int changed = 0;
  for (int i = 0; i < wlen; i++) {
    char c = l->text[start + i];
    char n;
    if (mode == 0)      n = (char)toupper((unsigned char)c);
    else if (mode == 1) n = (char)tolower((unsigned char)c);
    else                n = (i == 0) ? (char)toupper((unsigned char)c)
                                     : (char)tolower((unsigned char)c);
    xf[i] = n;
    if (n != c) changed = 1;
  }
  if (changed) {
    undo_begin_group(ed);
    for (int i = 0; i < wlen; i++) ed_delete(ed);          /* remove the word */
    for (int i = 0; i < wlen; i++) {                        /* insert transformed */
      char ch[2] = { xf[i], 0 };
      ed_insert_char(ed, ch);
    }
    undo_end_group(ed);
  } else {
    ed->cursor_col = end;
    ed->cursor_target_col = end;
  }
  free(xf);
}

/* C-t: transpose the two characters around point, advancing point. */
void ed_emacs_transpose_chars(EditorState *ed) {
  Line *l = &ed->lines[ed->cursor_line];
  if (l->len < 2) return;
  int a, b;
  if (ed->cursor_col >= l->len)      { a = l->len - 2; b = l->len - 1; }
  else if (ed->cursor_col == 0)      { a = 0;          b = 1;          }
  else                               { a = ed->cursor_col - 1; b = ed->cursor_col; }
  char x = l->text[a], y = l->text[b];
  if (x == y) { return; }
  undo_begin_group(ed);
  ed->cursor_col = a;
  ed_delete(ed);            /* remove x */
  ed_delete(ed);            /* remove y (now at a) */
  char ch[2];
  ch[0] = y; ch[1] = 0; ed_insert_char(ed, ch);
  ch[0] = x; ch[1] = 0; ed_insert_char(ed, ch);
  undo_end_group(ed);
}
