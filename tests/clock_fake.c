/* clock_fake.c — settable clock for the headless tests (implements clock.h). */
#include "clock_fake.h"

static unsigned int g_now;

unsigned int kern_now_ms(void) { return g_now; }
void kern_clock_set(unsigned int ms) { g_now = ms; }
