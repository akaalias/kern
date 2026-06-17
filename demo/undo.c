/* undo.c — Operation-based undo system */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "undo.h"
#include "buffer.h"

/* free the text field of a single undo op */
static void undo_op_free(UndoOp *op) {
  free(op->text);
  op->text = NULL;
  op->text_len = 0;
  op->type = UNDO_NONE;
}

void undo_push_op(EditorState *ed, UndoOpType type, int line, int col,
                   const char *text, int text_len)
{
  /* coalesce consecutive inserts at adjacent positions */
  if (type == UNDO_INSERT && ed->undo_top > 0) {
    UndoOp *prev = &ed->undo_stack[(ed->undo_top - 1) % MAX_UNDO];
    if (prev->type == UNDO_INSERT &&
        prev->line == line &&
        prev->col + prev->text_len == col)
    {
      /* extend the previous op */
      prev->text = realloc(prev->text, prev->text_len + text_len + 1);
      memcpy(prev->text + prev->text_len, text, text_len);
      prev->text_len += text_len;
      prev->text[prev->text_len] = '\0';
      return;
    }
  }

  /* wrap around: free the slot we're about to overwrite */
  UndoOp *slot = &ed->undo_stack[ed->undo_top % MAX_UNDO];
  undo_op_free(slot);

  slot->type = type;
  slot->line = line;
  slot->col = col;
  slot->cursor_line = ed->cursor_line;
  slot->cursor_col = ed->cursor_col;

  if (text && text_len > 0) {
    slot->text = malloc(text_len + 1);
    memcpy(slot->text, text, text_len);
    slot->text[text_len] = '\0';
    slot->text_len = text_len;
  } else {
    slot->text = NULL;
    slot->text_len = 0;
  }

  ed->undo_top++;
  if (ed->undo_top > MAX_UNDO) ed->undo_top = MAX_UNDO;
  ed->undo_count = ed->undo_top;
}

void undo_begin_group(EditorState *ed) {
  undo_push_op(ed, UNDO_GROUP_START, 0, 0, NULL, 0);
}

void undo_end_group(EditorState *ed) {
  undo_push_op(ed, UNDO_GROUP_END, 0, 0, NULL, 0);
}

/* undo a single (non-group) operation, directly manipulating the buffer */
static void undo_apply_one(EditorState *ed, UndoOp *op) {
  switch (op->type) {
    case UNDO_INSERT: {
      /* undo an insert: delete the inserted text */
      Line *l = &ed->lines[op->line];
      memmove(l->text + op->col, l->text + op->col + op->text_len,
              l->len - op->col - op->text_len + 1);
      l->len -= op->text_len;
      line_dirty(l);
      break;
    }
    case UNDO_DELETE: {
      /* undo a delete: re-insert the deleted text (may contain newlines) */
      /* check if text contains newlines */
      int has_nl = 0;
      for (int i = 0; i < op->text_len; i++) {
        if (op->text[i] == '\n') { has_nl = 1; break; }
      }
      if (!has_nl) {
        /* simple single-line re-insert */
        Line *l = &ed->lines[op->line];
        line_ensure_cap(l, l->len + op->text_len);
        memmove(l->text + op->col + op->text_len, l->text + op->col,
                l->len - op->col + 1);
        memcpy(l->text + op->col, op->text, op->text_len);
        l->len += op->text_len;
        line_dirty(l);
      } else {
        /* multi-line re-insert: split into lines and insert them */
        Line *l = &ed->lines[op->line];
        /* save the tail of the current line after the insertion point */
        int tail_len = l->len - op->col;
        char *tail = malloc(tail_len + 1);
        memcpy(tail, l->text + op->col, tail_len);
        tail[tail_len] = '\0';

        /* parse the text into segments separated by '\n' */
        const char *p = op->text;
        const char *end = op->text + op->text_len;
        int first_seg = 1;
        int cur_line = op->line;

        while (p <= end) {
          const char *nl = memchr(p, '\n', end - p);
          int seg_len = nl ? (int)(nl - p) : (int)(end - p);

          if (first_seg) {
            /* append first segment to the current line at op->col */
            l = &ed->lines[cur_line];
            l->len = op->col;
            l->text[l->len] = '\0';
            line_ensure_cap(l, l->len + seg_len);
            memcpy(l->text + l->len, p, seg_len);
            l->len += seg_len;
            l->text[l->len] = '\0';
            line_dirty(l);
            first_seg = 0;
          } else {
            /* insert a new line with this segment */
            cur_line++;
            buf_insert_line_at(ed, cur_line, p, seg_len);
          }

          if (!nl) break;
          p = nl + 1;
        }

        /* append the saved tail to the last line */
        l = &ed->lines[cur_line];
        line_ensure_cap(l, l->len + tail_len);
        memcpy(l->text + l->len, tail, tail_len);
        l->len += tail_len;
        l->text[l->len] = '\0';
        line_dirty(l);
        free(tail);
      }
      break;
    }
    case UNDO_SPLIT_LINE: {
      /* undo a split: join line and line+1 back together */
      int ln = op->line;
      if (ln + 1 < ed->line_count) {
        Line *cur = &ed->lines[ln];
        Line *next = &ed->lines[ln + 1];
        line_ensure_cap(cur, cur->len + next->len);
        memcpy(cur->text + cur->len, next->text, next->len + 1);
        cur->len += next->len;
        buf_delete_line_at(ed, ln + 1);
        line_dirty(cur);
      }
      break;
    }
    case UNDO_JOIN_LINE: {
      /* undo a join: split the line again at (line, col) */
      Line *l = &ed->lines[op->line];
      int rest_len = l->len - op->col;
      buf_insert_line_at(ed, op->line + 1, l->text + op->col, rest_len);
      l = &ed->lines[op->line]; /* re-fetch after possible realloc */
      l->len = op->col;
      l->text[l->len] = '\0';
      line_dirty(l);
      line_dirty(&ed->lines[op->line + 1]);
      break;
    }
    default:
      break;
  }
}

void undo_perform(EditorState *ed) {
  if (ed->undo_top == 0) return;

  ed->undo_top--;
  UndoOp *op = &ed->undo_stack[ed->undo_top % MAX_UNDO];

  if (op->type == UNDO_GROUP_END) {
    /* undo everything in the group until GROUP_START */
    int saved_cursor_line = op->cursor_line;
    int saved_cursor_col = op->cursor_col;

    while (ed->undo_top > 0) {
      ed->undo_top--;
      op = &ed->undo_stack[ed->undo_top % MAX_UNDO];
      if (op->type == UNDO_GROUP_START) {
        saved_cursor_line = op->cursor_line;
        saved_cursor_col = op->cursor_col;
        break;
      }
      undo_apply_one(ed, op);
      saved_cursor_line = op->cursor_line;
      saved_cursor_col = op->cursor_col;
    }

    ed->cursor_line = saved_cursor_line;
    ed->cursor_col = saved_cursor_col;
  } else {
    /* single op */
    undo_apply_one(ed, op);
    ed->cursor_line = op->cursor_line;
    ed->cursor_col = op->cursor_col;
  }

  /* clamp cursor */
  if (ed->cursor_line >= ed->line_count) ed->cursor_line = ed->line_count - 1;
  if (ed->cursor_line < 0) ed->cursor_line = 0;
  if (ed->cursor_col > ed->lines[ed->cursor_line].len)
    ed->cursor_col = ed->lines[ed->cursor_line].len;
  if (ed->cursor_col < 0) ed->cursor_col = 0;
}

void undo_clear(EditorState *ed) {
  for (int i = 0; i < MAX_UNDO; i++) {
    undo_op_free(&ed->undo_stack[i]);
  }
  ed->undo_top = 0;
  ed->undo_count = 0;
}
