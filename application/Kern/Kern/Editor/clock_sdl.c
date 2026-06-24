/* clock_sdl.c — SDL-backed clock (the app's implementation of clock.h).
 * Not compiled into the headless test build. */
#include <SDL2/SDL.h>
#include "clock.h"

unsigned int kern_now_ms(void) {
  return SDL_GetTicks();
}
