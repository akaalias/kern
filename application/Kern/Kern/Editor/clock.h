/* clock.h — monotonic millisecond clock behind a seam.
 *
 * Keeps time-dependent logic (status-message expiry, autosave) off a direct
 * SDL call so tests can drive time deterministically. The app links the
 * SDL-backed implementation (clock_sdl.c); tests link a settable fake. */
#ifndef CLOCK_H
#define CLOCK_H

/* Milliseconds since some fixed, arbitrary origin (monotonic). */
unsigned int kern_now_ms(void);

#endif /* CLOCK_H */
