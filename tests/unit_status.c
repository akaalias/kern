/* unit_status.c — unit tests for navigation.c's status line, driving time
 * deterministically through the fake clock seam. */
#include <stdio.h>
#include <string.h>
#include "test.h"
#include "ed_fixture.h"
#include "navigation.h"
#include "clock_fake.h"

/* A transient message shows until STATUS_DURATION elapses, then clears. */
static void test_status_message_expires(void) {
  EditorState ed = {0}; buf_init_empty(&ed);
  ViewState vs = {0};

  kern_clock_set(1000);
  nav_status_set(&vs, "Saved");
  CHECK_SEQ(nav_status_get(&ed, &vs), "Saved");

  kern_clock_set(1000 + STATUS_DURATION - 1);
  CHECK_SEQ(nav_status_get(&ed, &vs), "Saved");   /* still within the window */

  kern_clock_set(1000 + STATUS_DURATION);
  CHECK_SEQ(nav_status_get(&ed, &vs), "");        /* expired */

  ed_teardown(&ed);
}

/* Mode indicators take priority over a transient message. */
static void test_status_priority(void) {
  EditorState ed = {0}; buf_init_empty(&ed);
  ViewState vs = {0};

  kern_clock_set(0);
  nav_status_set(&vs, "Saved");

  ed.mark_active = 1;
  CHECK_SEQ(nav_status_get(&ed, &vs), "Mark active");
  ed.mark_active = 0;

  vs.esc_prefix = 1;
  CHECK_SEQ(nav_status_get(&ed, &vs), "ESC-");
  vs.esc_prefix = 0;

  vs.ctrl_x_prefix = 1;
  CHECK_SEQ(nav_status_get(&ed, &vs), "C-x -");
  vs.ctrl_x_prefix = 0;

  vs.search_active = 1; vs.search_direction = 1;
  CHECK_SEQ(nav_status_get(&ed, &vs), "I-search:");
  vs.search_direction = -1;
  CHECK_SEQ(nav_status_get(&ed, &vs), "I-search backward:");
  vs.search_active = 0;

  vs.minibuf_active = 1;
  snprintf(vs.minibuf_prompt, sizeof vs.minibuf_prompt, "Find file: ");
  CHECK_SEQ(nav_status_get(&ed, &vs), "Find file: ");

  ed_teardown(&ed);
}

void suite_status(void) {
  RUN(test_status_message_expires);
  RUN(test_status_priority);
}
