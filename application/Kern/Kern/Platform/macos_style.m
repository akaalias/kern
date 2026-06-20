#import <Cocoa/Cocoa.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

/* from buffer.c — the sandbox-container Documents directory */
extern const char *buf_get_documents_dir(void);

/* Target for the title-bar folder button. Retained for the app's lifetime. */
@interface KernTitlebarActions : NSObject
- (void)openDocsFolder:(id)sender;
@end

@implementation KernTitlebarActions
- (void)openDocsFolder:(id)sender {
  (void)sender;
  const char *p = buf_get_documents_dir();
  if (!p || !*p) return;
  NSString *path = [NSString stringWithUTF8String:p];
  [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:path isDirectory:YES]];
}
@end

static KernTitlebarActions *g_titlebar_actions;  /* strong (ARC) — keep alive */

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

  /* icon-only button in the top-right of the title bar: open the documents
     folder in Finder */
  g_titlebar_actions = [KernTitlebarActions new];
  NSImage *icon = [NSImage imageWithSystemSymbolName:@"folder"
                            accessibilityDescription:@"Open documents folder"];
  NSButton *btn = [NSButton buttonWithImage:icon
                                     target:g_titlebar_actions
                                     action:@selector(openDocsFolder:)];
  btn.bordered = NO;
  btn.imagePosition = NSImageOnly;
  btn.toolTip = @"Open documents folder in Finder";
  btn.frame = NSMakeRect(0, 0, 38, 30);

  NSTitlebarAccessoryViewController *acc = [[NSTitlebarAccessoryViewController alloc] init];
  acc.layoutAttribute = NSLayoutAttributeRight;
  acc.view = btn;
  [nswindow addTitlebarAccessoryViewController:acc];
}
