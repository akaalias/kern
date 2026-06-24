/* unit_recent.c — unit tests for Editor/recent.c (MRU list + path_base). */
#include <stdio.h>
#include <string.h>
#include "test.h"
#include "recent.h"

static void test_path_base(void) {
  CHECK_SEQ(path_base("/a/b/c.md"), "c.md");
  CHECK_SEQ(path_base("nodir.md"), "nodir.md");
  CHECK_SEQ(path_base("/trailing/"), "");
  CHECK_SEQ(path_base("/"), "");
}

static void test_push_and_order(void) {
  recent_reset();
  recent_push("/docs/a.md");
  recent_push("/docs/b.md");
  CHECK_IEQ(recent_count(), 2);
  CHECK_SEQ(recent_get(0), "/docs/b.md");   /* most recent first */
  CHECK_SEQ(recent_get(1), "/docs/a.md");
  CHECK(recent_get(2) == NULL);
  CHECK(recent_get(-1) == NULL);
}

static void test_push_dedup_moves_to_front(void) {
  recent_reset();
  recent_push("/a"); recent_push("/b"); recent_push("/c");
  recent_push("/a");                         /* re-open → front, no duplicate */
  CHECK_IEQ(recent_count(), 3);
  CHECK_SEQ(recent_get(0), "/a");
  CHECK_SEQ(recent_get(1), "/c");
  CHECK_SEQ(recent_get(2), "/b");
}

static void test_push_ignores_empty(void) {
  recent_reset();
  recent_push("");
  recent_push(NULL);
  CHECK_IEQ(recent_count(), 0);
}

static void test_push_caps_at_max(void) {
  recent_reset();
  char p[32];
  for (int i = 0; i < RECENT_MAX + 10; i++) {
    snprintf(p, sizeof p, "/f%d", i);
    recent_push(p);
  }
  CHECK_IEQ(recent_count(), RECENT_MAX);
  snprintf(p, sizeof p, "/f%d", RECENT_MAX + 9);  /* newest is at the front */
  CHECK_SEQ(recent_get(0), p);
}

void suite_recent(void) {
  RUN(test_path_base);
  RUN(test_push_and_order);
  RUN(test_push_dedup_moves_to_front);
  RUN(test_push_ignores_empty);
  RUN(test_push_caps_at_max);
}
