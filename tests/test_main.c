/* test_main.c — runner + shared counters for the headless test suite.
 * Each unit_*.c exposes one suite_* entry point; list them in main(). */
#include <stdio.h>
#include "test.h"

int kt_checks = 0;
int kt_failed_checks = 0;
int kt_cur_failed = 0;

static int kt_tests = 0;
static int kt_tests_failed = 0;

void kt_run(const char *name, kt_test_fn fn) {
  kt_cur_failed = 0;
  fn();
  kt_tests++;
  if (kt_cur_failed) {
    kt_tests_failed++;
    fprintf(stderr, "not ok  %s  (%d failed)\n", name, kt_cur_failed);
  } else {
    fprintf(stdout, "ok      %s\n", name);
  }
}

/* suite entry points — one per unit_*.c */
void suite_editing(void);
void suite_undo(void);
void suite_buffer(void);
void suite_navigation(void);
void suite_search(void);
void suite_md_render(void);
void suite_recent(void);
void suite_status(void);
void suite_commands(void);
void suite_utf8(void);
void suite_textview(void);
void suite_snapshot(void);

int main(void) {
  suite_editing();
  suite_undo();
  suite_buffer();
  suite_navigation();
  suite_search();
  suite_md_render();
  suite_recent();
  suite_status();
  suite_commands();
  suite_utf8();
  suite_textview();
  suite_snapshot();

  fprintf(stdout, "\n%d tests (%d failed), %d checks (%d failed)\n",
          kt_tests, kt_tests_failed, kt_checks, kt_failed_checks);
  return (kt_tests_failed || kt_failed_checks) ? 1 : 0;
}
