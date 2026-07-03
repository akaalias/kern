#import <Cocoa/Cocoa.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

/* from buffer.c — the sandbox-container Documents directory */
extern const char *buf_get_documents_dir(void);

/* from textview.c — publish the current note to X (runs on the main thread) */
extern void kern_publish_to_x(void);
/* from KernApp.swift (@_cdecl) — 1 if an X account is linked */
extern int kern_x_is_connected(void);
/* from textview.c — current on/off state of the View-menu toggles */
extern int kern_syntax_enabled(void);
extern int kern_style_enabled(void);
extern int kern_verbs_enabled(void);
extern int kern_nouns_enabled(void);
extern int kern_adjectives_enabled(void);
extern int kern_adverbs_enabled(void);
extern int kern_function_words_enabled(void);
extern int kern_fillers_enabled(void);
extern int kern_cliches_enabled(void);
extern int kern_redundancies_enabled(void);

/* Open the sandbox-container Documents folder in Finder. Bridged to the Window
   menu item (App/KernApp.swift); runs on the main thread during menu tracking. */
void kern_open_documents_folder(void) {
  const char *p = buf_get_documents_dir();
  if (!p || !*p) return;
  NSString *path = [NSString stringWithUTF8String:p];
  [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:path isDirectory:YES]];
}

/* CFBundleShortVersionString (the marketing version), for the dev-build label. */
const char *kern_app_version(void) {
  static char buf[32];
  if (buf[0] == '\0') {
    NSString *v = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
    snprintf(buf, sizeof(buf), "%s", v ? v.UTF8String : "?");
  }
  return buf;
}


/* Target for the title-bar publish button. Retained for the app's lifetime. */
@interface KernTitlebarActions : NSObject
- (void)publishToX:(id)sender;
@end

@implementation KernTitlebarActions

- (void)publishToX:(id)sender {
  (void)sender;
  /* the title-bar button only appears when X is connected; the C side reads the
     current note/region and hands it to the async Swift publisher. */
  kern_publish_to_x();
}
@end

static KernTitlebarActions *g_titlebar_actions;  /* strong (ARC) — keep alive */
static NSButton *g_publish_btn;                  /* the "Publish to X" title-bar button */

/* Called from the editor loop (main thread) when the X connection state flips.
   The button lives in an NSStackView, so toggling `hidden` collapses/expands its
   slot with no leftover gap. */
void kern_titlebar_set_x_connected(int connected) {
  g_publish_btn.hidden = connected ? NO : YES;
}

#pragma mark - Window chrome

static NSButton *kern_titlebar_button(NSString *symbol, NSString *accDesc,
                                      NSString *tip, SEL action, NSRect frame) {
  NSImage *icon = [NSImage imageWithSystemSymbolName:symbol
                            accessibilityDescription:accDesc];
  NSButton *btn = [NSButton buttonWithImage:icon
                                     target:g_titlebar_actions
                                     action:action];
  btn.bordered = NO;
  btn.imagePosition = NSImageOnly;
  btn.toolTip = tip;
  btn.frame = frame;
  return btn;
}

/* Live checkmarks for the View-menu toggles. SwiftUI builds the "Syntax
   Highlighting" / "Style Check" items (via .commands) but can't update them while
   the main run loop is parked. So we own the View menu's delegate: AppKit calls
   menuNeedsUpdate: on the main thread just before the menu opens — we chain to
   SwiftUI's delegate (so its items still populate), then set each item's checkmark
   from the live C state. */
@interface KernViewMenuDelegate : NSObject <NSMenuDelegate>
@property (nonatomic, weak) id<NSMenuDelegate> next;
@end
@implementation KernViewMenuDelegate
- (void)menuNeedsUpdate:(NSMenu *)menu {
  if ([self.next respondsToSelector:@selector(menuNeedsUpdate:)])
    [self.next menuNeedsUpdate:menu];
  /* Titles must match the SwiftUI .commands buttons (App/KernApp.swift) exactly. */
#define KERN_SETCHK(t, fn) [menu itemWithTitle:(t)].state = \
    (fn)() ? NSControlStateValueOn : NSControlStateValueOff
  KERN_SETCHK(@"Syntax Highlighting", kern_syntax_enabled);
  KERN_SETCHK(@"Verbs",               kern_verbs_enabled);
  KERN_SETCHK(@"Nouns",               kern_nouns_enabled);
  KERN_SETCHK(@"Adjectives",          kern_adjectives_enabled);
  KERN_SETCHK(@"Adverbs",             kern_adverbs_enabled);
  KERN_SETCHK(@"Function Words",      kern_function_words_enabled);
  KERN_SETCHK(@"Style Check",         kern_style_enabled);
  KERN_SETCHK(@"Fillers",             kern_fillers_enabled);
  KERN_SETCHK(@"Cliches",             kern_cliches_enabled);
  KERN_SETCHK(@"Redundancies",        kern_redundancies_enabled);
#undef KERN_SETCHK
}
@end

static KernViewMenuDelegate *g_view_menu_delegate;  /* strong (ARC) — keep alive */

static void kern_install_view_menu_checkmarks(void) {
  if (g_view_menu_delegate) return;
  NSMenu *view = [NSApp.mainMenu itemWithTitle:@"View"].submenu;
  if (!view) return;
  g_view_menu_delegate = [KernViewMenuDelegate new];
  g_view_menu_delegate.next = view.delegate;
  view.delegate = g_view_menu_delegate;
}

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

  /* icon-only buttons in the top-right of the title bar: a help button that
     lists the keyboard shortcuts, a folder button that opens the documents
     folder in Finder, and a publish button that posts the current note to X
     (shown only while an X account is connected). */
  g_titlebar_actions = [KernTitlebarActions new];
  g_publish_btn = kern_titlebar_button(@"paperplane", @"Publish to X",
                                       @"Publish this note to X",
                                       @selector(publishToX:),
                                       NSZeroRect);
  /* An actual labelled button: icon + "Publish", a rounded pill in the title
     bar (was an icon-only glyph). */
  g_publish_btn.title = @"Publish";
  g_publish_btn.bordered = YES;
  g_publish_btn.bezelStyle = NSBezelStyleRounded;
  g_publish_btn.imagePosition = NSImageLeft;
  g_publish_btn.hidden = (kern_x_is_connected() == 0);   /* until/unless connected */

  /* AppKit stretches the accessory view to the full (toolbar-tall) title bar
     height, its top-right flush with the window corner. Pin the publish button
     16pt from the top and 16pt from the trailing edge so its distance from the
     top window edge matches its distance from the right window edge; width is
     intrinsic (icon + "Publish"). (The keyboard-shortcuts and documents-folder
     buttons moved to the Settings window and the Window menu respectively.) */
  NSView *buttons = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 130, 44)];
  g_publish_btn.translatesAutoresizingMaskIntoConstraints = NO;
  [buttons addSubview:g_publish_btn];
  [NSLayoutConstraint activateConstraints:@[
    [g_publish_btn.heightAnchor constraintEqualToConstant:24],
    [g_publish_btn.trailingAnchor constraintEqualToAnchor:buttons.trailingAnchor constant:-16],
    [g_publish_btn.topAnchor constraintEqualToAnchor:buttons.topAnchor constant:16],
  ]];

  NSTitlebarAccessoryViewController *acc = [[NSTitlebarAccessoryViewController alloc] init];
  acc.layoutAttribute = NSLayoutAttributeRight;
  acc.view = buttons;
  [nswindow addTitlebarAccessoryViewController:acc];

  kern_install_view_menu_checkmarks();   /* live ✓ on the View-menu toggles */
}
