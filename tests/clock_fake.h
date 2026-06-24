/* clock_fake.h — test control over the seam clock (see clock.h). */
#ifndef KERN_CLOCK_FAKE_H
#define KERN_CLOCK_FAKE_H

#include "clock.h"   /* kern_now_ms */

/* Set the value kern_now_ms() returns. */
void kern_clock_set(unsigned int ms);

#endif /* KERN_CLOCK_FAKE_H */
