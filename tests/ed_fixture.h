/* ed_fixture.h — shared EditorState fixtures for the headless unit suites. */
#ifndef KERN_ED_FIXTURE_H
#define KERN_ED_FIXTURE_H

#include <stdlib.h>
#include <string.h>
#include "editor_types.h"
#include "buffer.h"

#define LINE(ed, i) ((ed).lines[(i)].text)

/* Fresh single-line buffer holding `s`, with the caret at end of the line. */
static inline void ed_load(EditorState *ed, const char *s) {
  buf_init_empty(ed);
  int n = (int)strlen(s);
  line_ensure_cap(&ed->lines[0], n);
  memcpy(ed->lines[0].text, s, n + 1);
  ed->lines[0].len = n;
  ed->cursor_col = n;
  ed->cursor_target_col = n;
}

/* Free everything an EditorState owns so LeakSanitizer stays quiet. */
static inline void ed_teardown(EditorState *ed) {
  buf_free_all_lines(ed);
  free(ed->lines);
  free(ed->kill_buf);
  for (int i = 0; i < MAX_UNDO; i++) free(ed->undo_stack[i].text);
  memset(ed, 0, sizeof *ed);
}

#endif /* KERN_ED_FIXTURE_H */
