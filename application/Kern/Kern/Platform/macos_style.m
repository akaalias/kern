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
extern int kern_typewriter_enabled(void);
extern int kern_subs_enabled(void);
extern int kern_page_borders_enabled(void);

/* Open the sandbox-container Documents folder in Finder. Bridged to the Window
   menu item (App/KernApp.swift); runs on the main thread during menu tracking. */
void kern_open_documents_folder(void) {
  const char *p = buf_get_documents_dir();
  if (!p || !*p) return;
  NSString *path = [NSString stringWithUTF8String:p];
  [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:path isDirectory:YES]];
}

/* Open a URL in the user's default browser (Cmd-Enter over a link in the text).
   NSWorkspace launches it asynchronously, so this is safe to call inline on the
   main thread during event handling. */
void kern_open_url(const char *url) {
  if (!url || !*url) return;
  NSString *s = [NSString stringWithUTF8String:url];
  NSURL *u = [NSURL URLWithString:s];
  if (u) [[NSWorkspace sharedWorkspace] openURL:u];
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

/* Menu decoration: live checkmarks + chord-as-text shortcut hints.
   SwiftUI builds the items (via .commands) but can't update them while the main
   run loop is parked, and macOS menu items can't display two-chord emacs
   sequences ("⌃X ⌃S") in the native shortcut column. So we own each menu's
   delegate: AppKit calls menuNeedsUpdate: on the main thread just before the
   menu opens — we chain to SwiftUI's delegate (so its items still populate),
   then set each item's checkmark from the live C state and append its chord in
   small secondary-color text via attributedTitle. (itemWithTitle: keeps
   matching after attributedTitle is set — the plain title is untouched.) */

typedef struct {
  const char *title;      /* must match the SwiftUI Button title EXACTLY */
  const char *chord;      /* shortcut hint text, NULL for none */
  int (*enabled)(void);   /* checkmark source, NULL for plain items */
} KernItemSpec;

static const KernItemSpec k_file_items[] = {
  {"Open…", "⌃X ⌃F", NULL}, {"Save", "⌃X ⌃S", NULL}, {"Save As…", "⌃X ⌃W", NULL},
  {"Switch to Recent Buffer", "⌃X B", NULL}, {"Today's Note", "⌘⇧T", NULL},
  {NULL, NULL, NULL},
};
static const KernItemSpec k_edit_items[] = {
  {"Undo", "⌃/", NULL},
  {"Cut", "⌃W", NULL}, {"Copy", "⌥W / ⌘C", NULL}, {"Paste", "⌃Y / ⌘V", NULL},
  {"Select All", "⌃X H", NULL},
  {"Kill to End of Line", "⌃K", NULL},
  {"Delete Word Forward", "⌥D", NULL}, {"Delete Word Backward", "⌥⌫", NULL},
  {"Transpose Characters", "⌃T", NULL}, {"Insert Blank Line", "⌃O", NULL},
  {"UPPERCASE Word", "⌥U", NULL}, {"lowercase Word", "⌥L", NULL},
  {"Capitalize Word", "⌥C", NULL},
  {"Search Forward", "⌃S", NULL}, {"Search Backward", "⌃R", NULL},
  {NULL, NULL, NULL},
};
static const KernItemSpec k_format_items[] = {
  {"Bold", "**", NULL}, {"Italic", "_", NULL}, {"Highlight", "==", NULL},
  {"Underline", "++", NULL}, {"Inline Code", "`", NULL},
  {"Highlight Sentence", "⌘⇧H", NULL}, {"Underline Sentence", "⌘⇧U", NULL},
  {"Indent List Item", "⇥", NULL}, {"Outdent List Item", "⇧⇥", NULL},
  {NULL, NULL, NULL},
};
static const KernItemSpec k_view_items[] = {
  {"Typewriter Mode", "⌃X T", kern_typewriter_enabled},
  {"Symbols", "⌃X L", kern_subs_enabled},
  {"Page Borders", "⌃X P", kern_page_borders_enabled},
  {"Syntax Highlighting", "⌃X Y", kern_syntax_enabled},
  {"Verbs", NULL, kern_verbs_enabled}, {"Nouns", NULL, kern_nouns_enabled},
  {"Adjectives", NULL, kern_adjectives_enabled},
  {"Adverbs", NULL, kern_adverbs_enabled},
  {"Function Words", NULL, kern_function_words_enabled},
  {"Style Check", "⌃X S", kern_style_enabled},
  {"Fillers", NULL, kern_fillers_enabled}, {"Cliches", NULL, kern_cliches_enabled},
  {"Redundancies", NULL, kern_redundancies_enabled},
  {"Bigger Text", "⌘=", NULL}, {"Smaller Text", "⌘-", NULL},
  {"Recenter", "⌃L", NULL}, {"Page Down", "⌃V", NULL}, {"Page Up", "⌥V", NULL},
  {NULL, NULL, NULL},
};
static const KernItemSpec k_go_items[] = {
  {"Top of Document", "⌘⇧,", NULL}, {"Bottom of Document", "⌘⇧.", NULL},
  {"Go to Line…", "⌥G", NULL},
  {"Follow Link", "⌘↩", NULL}, {"Back", "⌘⇧←", NULL}, {"Forward", "⌘⇧→", NULL},
  {NULL, NULL, NULL},
};
static const KernItemSpec k_notes_items[] = {
  {"Extract Selection to New Note", "⌘⇧N", NULL}, {"Margin Note", "⌘⇧M", NULL},
  {"Download News Feed", "⌃X N", NULL}, {"Download Bookmarks", "⌃X M", NULL},
  {NULL, NULL, NULL},
};
static const struct { const char *menu; const KernItemSpec *items; } k_menus[] = {
  {"File", k_file_items}, {"Edit", k_edit_items}, {"Format", k_format_items},
  {"View", k_view_items}, {"Go", k_go_items}, {"Notes", k_notes_items},
  {NULL, NULL},
};

/* Decorate the menu items directly from the editor tick (main thread, ~250ms):
   set each toggle's live ✓ state and append its chord hint. NO NSMenuDelegate —
   we tried owning each menu's delegate and lost the delegate war: SwiftUI
   re-grabs menu delegates on its own schedule, so our menuNeedsUpdate: fired
   once and never again, freezing every checkmark at its first-open value
   (hints survived only because attributedTitle is sticky). Setting item state
   from the poll needs no cooperation from AppKit or SwiftUI: items update
   while the menu is closed and display whatever is current when it opens.
   (Menu tracking blocks the SDL loop, so states can't go stale mid-display —
   any toggle closes the menu first, and the next tick runs before it can be
   reopened.) */
/* Find a spec's item by its plain title OR by its decorated title. Gotcha:
   setting attributedTitle makes AppKit sync the plain `title` to the full
   decorated string ("Save    ⌃X ⌃S"), so a plain itemWithTitle: match stops
   working after the first decoration pass — every decorated toggle froze at
   its first-tick state until this looked past the appended hint. */
static NSMenuItem *kern_find_item(NSMenu *menu, const char *title) {
  NSString *t = @(title);
  NSString *decorated = [t stringByAppendingString:@"    "];
  for (NSMenuItem *it in menu.itemArray) {
    if ([it.title isEqualToString:t] || [it.title hasPrefix:decorated]) return it;
  }
  return nil;
}

void kern_menus_sync(void) {
  for (int i = 0; k_menus[i].menu; i++) {
    NSMenu *menu = [NSApp.mainMenu itemWithTitle:@(k_menus[i].menu)].submenu;
    if (!menu) continue;                     /* not built yet — retry next tick */
    for (const KernItemSpec *s = k_menus[i].items; s->title; s++) {
      NSMenuItem *item = kern_find_item(menu, s->title);
      if (!item) continue;
      if (s->enabled)
        item.state = s->enabled() ? NSControlStateValueOn : NSControlStateValueOff;
      /* Chord hint, set once per item (SwiftUI may rebuild items — a fresh
         item has no attributedTitle yet and gets re-decorated next tick). */
      if (s->chord && !item.attributedTitle) {
        NSMutableAttributedString *t = [[NSMutableAttributedString alloc]
            initWithString:@(s->title)
                attributes:@{NSFontAttributeName: [NSFont menuFontOfSize:0]}];
        /* @() decodes the C string as UTF-8; %s would misread the multi-byte
           key symbols (⌃⌥⇧⌘…) in the legacy system encoding and show mojibake. */
        NSString *hint = [NSString stringWithFormat:@"    %@", @(s->chord)];
        [t appendAttributedString:[[NSAttributedString alloc]
            initWithString:hint
                attributes:@{
                  NSFontAttributeName: [NSFont menuFontOfSize:[NSFont smallSystemFontSize]],
                  NSForegroundColorAttributeName: [NSColor secondaryLabelColor],
                }]];
        item.attributedTitle = t;
      }
    }
    /* macOS injects "Enter Full Screen" into the View menu automatically; it
       doesn't work with the SDL window, so strip it whenever it (re)appears. */
    if (strcmp(k_menus[i].menu, "View") == 0) {
      for (NSInteger j = menu.numberOfItems - 1; j >= 0; j--) {
        if ([menu itemAtIndex:j].action == @selector(toggleFullScreen:))
          [menu removeItemAtIndex:j];
      }
    }
  }
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

  kern_menus_sync();   /* checkmarks + chord hints on the menus (re-synced each tick) */

  /* SwiftUI populates each menu's REAL items lazily, on its first display —
     replacing the items the tick decorated, mid-open, while menu tracking has
     the SDL loop (and so the tick) frozen. Without help, the first-ever open
     of each menu shows bare items (no ✓ gutter, no chord hints, Enter Full
     Screen back). [menu update] at startup does NOT trigger that lazy build,
     so instead: a timer registered ONLY in NSEventTrackingRunLoopMode — it
     never fires in normal operation (zero cost), but menu tracking runs the
     run loop in exactly that mode, so while a menu is open it ticks every
     0.1s and re-decorates the freshly swapped-in items live. */
  NSTimer *t = [NSTimer timerWithTimeInterval:0.1 repeats:YES
                                        block:^(NSTimer *timer) { kern_menus_sync(); }];
  [[NSRunLoop mainRunLoop] addTimer:t forMode:NSEventTrackingRunLoopMode];
}
