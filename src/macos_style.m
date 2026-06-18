#import <Cocoa/Cocoa.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

void macos_style_window(SDL_Window *sdl_window) {
  SDL_SysWMinfo info;
  SDL_VERSION(&info.version);
  if (!SDL_GetWindowWMInfo(sdl_window, &info)) return;

  NSWindow *nswindow = info.info.cocoa.window;

  nswindow.appearance = [NSAppearance appearanceNamed:NSAppearanceNameVibrantDark];
  nswindow.titlebarAppearsTransparent = YES;
  nswindow.titleVisibility = NSWindowTitleVisible;

  /* let GL content extend behind the title bar — eliminates the black separator */
  nswindow.styleMask |= NSWindowStyleMaskFullSizeContentView;

  nswindow.backgroundColor = [NSColor colorWithRed:30.0/255.0
                                             green:30.0/255.0
                                              blue:32.0/255.0
                                             alpha:1.0];

  /* toolbar for taller title bar */
  NSToolbar *toolbar = [[NSToolbar alloc] initWithIdentifier:@"main"];
  nswindow.toolbar = toolbar;
}
