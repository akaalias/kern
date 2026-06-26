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

/* CFBundleShortVersionString (the marketing version), for the dev-build label. */
const char *kern_app_version(void) {
  static char buf[32];
  if (buf[0] == '\0') {
    NSString *v = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
    snprintf(buf, sizeof(buf), "%s", v ? v.UTF8String : "?");
  }
  return buf;
}

#pragma mark - Keyboard-shortcut sheet

/* A small rounded "key-cap" view that draws one key (e.g. ⌃ or "X"). */
@interface KernKeycap : NSView
@property(nonatomic, copy) NSString *text;
@end

@implementation KernKeycap
- (instancetype)initWithText:(NSString *)text {
  if ((self = [super initWithFrame:NSZeroRect])) {
    _text = [text copy];
    self.translatesAutoresizingMaskIntoConstraints = NO;
  }
  return self;
}
- (NSDictionary *)textAttributes {
  return @{
    NSFontAttributeName : [NSFont systemFontOfSize:12.5 weight:NSFontWeightMedium],
    NSForegroundColorAttributeName : [NSColor colorWithWhite:0.92 alpha:1.0],
  };
}
- (NSSize)intrinsicContentSize {
  NSSize t = [self.text sizeWithAttributes:[self textAttributes]];
  CGFloat w = ceil(t.width) + 16.0;
  if (w < 26.0) w = 26.0;            /* keep single glyphs roughly square */
  return NSMakeSize(w, 24.0);
}
- (void)drawRect:(NSRect)dirty {
  (void)dirty;
  NSRect r = NSInsetRect(self.bounds, 0.5, 0.5);
  NSBezierPath *p = [NSBezierPath bezierPathWithRoundedRect:r xRadius:5.0 yRadius:5.0];
  [[NSColor colorWithWhite:1.0 alpha:0.08] setFill];
  [p fill];
  [[NSColor colorWithWhite:1.0 alpha:0.20] setStroke];
  p.lineWidth = 1.0;
  [p stroke];

  NSDictionary *attrs = [self textAttributes];
  NSSize t = [self.text sizeWithAttributes:attrs];
  NSPoint at = NSMakePoint(NSMidX(self.bounds) - t.width / 2.0,
                           NSMidY(self.bounds) - t.height / 2.0);
  [self.text drawAtPoint:at withAttributes:attrs];
}
@end

/* A top-origin (flipped) container so the scroll view shows content from the top. */
@interface KernFlippedView : NSView
@end
@implementation KernFlippedView
- (BOOL)isFlipped { return YES; }
@end

/* Target for the title-bar buttons. Retained for the app's lifetime. */
@interface KernTitlebarActions : NSObject <NSWindowDelegate>
- (void)openDocsFolder:(id)sender;
- (void)showShortcuts:(id)sender;
- (void)closeShortcuts:(id)sender;
- (void)publishToX:(id)sender;
@end

static NSPanel *g_shortcuts_panel;  /* strong while the panel is up */

@implementation KernTitlebarActions

- (void)openDocsFolder:(id)sender {
  (void)sender;
  const char *p = buf_get_documents_dir();
  if (!p || !*p) return;
  NSString *path = [NSString stringWithUTF8String:p];
  [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:path isDirectory:YES]];
}

- (void)publishToX:(id)sender {
  (void)sender;
  /* the title-bar button only appears when X is connected; the C side reads the
     current note/region and hands it to the async Swift publisher. */
  kern_publish_to_x();
}

/* Make a label NSTextField (non-editable, no background). */
static NSTextField *kern_label(NSString *s, CGFloat size, NSFontWeight weight,
                               NSColor *color) {
  NSTextField *f = [NSTextField labelWithString:s];
  f.font = [NSFont systemFontOfSize:size weight:weight];
  f.textColor = color;
  f.translatesAutoresizingMaskIntoConstraints = NO;
  return f;
}

/* Parse an emacs-style chord string ("C-x C-s", "M-w", "Cmd-=") into a row of
   key-cap views. " / " separates interchangeable alternatives (joined by "or");
   a space separates chords pressed in sequence (joined by "then"); keys within a
   chord are pressed together. */
static NSView *kern_chord_view(NSString *spec) {
  NSStackView *row = [[NSStackView alloc] init];
  row.orientation = NSUserInterfaceLayoutOrientationHorizontal;
  row.spacing = 6.0;
  row.alignment = NSLayoutAttributeCenterY;
  row.translatesAutoresizingMaskIntoConstraints = NO;

  NSColor *dim = [NSColor colorWithWhite:0.55 alpha:1.0];
  NSArray<NSString *> *alts = [spec componentsSeparatedByString:@" / "];
  BOOL firstAlt = YES;
  for (NSString *alt in alts) {
    if (!firstAlt) {
      [row addArrangedSubview:kern_label(@"or", 11.0, NSFontWeightRegular, dim)];
    }
    firstAlt = NO;

    NSArray<NSString *> *chords = [alt componentsSeparatedByString:@" "];
    BOOL firstChord = YES;
    for (NSString *chord in chords) {
      if (chord.length == 0) continue;
      if (!firstChord) {
        [row addArrangedSubview:kern_label(@"then", 11.0, NSFontWeightRegular, dim)];
      }
      firstChord = NO;

      /* a tight group of caps pressed together */
      NSStackView *group = [[NSStackView alloc] init];
      group.orientation = NSUserInterfaceLayoutOrientationHorizontal;
      group.spacing = 3.0;
      group.translatesAutoresizingMaskIntoConstraints = NO;

      NSString *rest = chord;
      /* peel modifier prefixes off the front */
      struct { NSString *pfx; NSString *cap; } mods[] = {
        { @"C-", @"⌃" },   /* ⌃ Control */
        { @"M-", @"⌥" },   /* ⌥ Option  */
        { @"S-", @"⇧" },   /* ⇧ Shift   */
        { @"Cmd-", @"⌘" }, /* ⌘ Command */
      };
      BOOL matched = YES;
      while (matched && rest.length > 0) {
        matched = NO;
        for (unsigned i = 0; i < sizeof(mods) / sizeof(mods[0]); i++) {
          if ([rest hasPrefix:mods[i].pfx]) {
            [group addArrangedSubview:[[KernKeycap alloc] initWithText:mods[i].cap]];
            rest = [rest substringFromIndex:mods[i].pfx.length];
            matched = YES;
            break;
          }
        }
      }
      /* the remaining key — single letters uppercased, named keys kept as-is */
      NSString *key = rest;
      if (key.length == 1) key = [key uppercaseString];
      if (key.length > 0)
        [group addArrangedSubview:[[KernKeycap alloc] initWithText:key]];

      [row addArrangedSubview:group];
    }
  }
  return row;
}

/* Build one description-left / keys-right row. */
static NSView *kern_binding_row(NSString *desc, NSString *spec) {
  NSStackView *r = [[NSStackView alloc] init];
  r.orientation = NSUserInterfaceLayoutOrientationHorizontal;
  r.spacing = 12.0;
  r.alignment = NSLayoutAttributeCenterY;
  r.translatesAutoresizingMaskIntoConstraints = NO;

  NSTextField *label = kern_label(desc, 13.0, NSFontWeightRegular,
                                  [NSColor colorWithWhite:0.85 alpha:1.0]);
  NSView *spacer = [[NSView alloc] init];
  spacer.translatesAutoresizingMaskIntoConstraints = NO;
  [spacer setContentHuggingPriority:1 forOrientation:NSLayoutConstraintOrientationHorizontal];

  [r addArrangedSubview:label];
  [r addArrangedSubview:spacer];
  [r addArrangedSubview:kern_chord_view(spec)];
  return r;
}

/* Wrap content in a layer-backed bordered "bento" card. The inner view is
   pinned by inset constraints, so the card's height is driven by its content. */
static NSView *kern_card_wrap(NSView *inner) {
  NSView *card = [[NSView alloc] init];
  card.translatesAutoresizingMaskIntoConstraints = NO;
  card.wantsLayer = YES;
  card.layer.cornerRadius = 10.0;
  card.layer.borderWidth = 1.0;
  card.layer.borderColor = [NSColor colorWithWhite:1.0 alpha:0.12].CGColor;
  card.layer.backgroundColor = [NSColor colorWithWhite:1.0 alpha:0.035].CGColor;
  [card addSubview:inner];
  [NSLayoutConstraint activateConstraints:@[
    [inner.topAnchor constraintEqualToAnchor:card.topAnchor constant:14],
    [inner.bottomAnchor constraintEqualToAnchor:card.bottomAnchor constant:-14],
    [inner.leadingAnchor constraintEqualToAnchor:card.leadingAnchor constant:16],
    [inner.trailingAnchor constraintEqualToAnchor:card.trailingAnchor constant:-16],
  ]];
  return card;
}

/* One bento card for a group: a title plus its binding rows. */
static NSView *kern_group_card(NSArray *section) {
  NSStackView *inner = [[NSStackView alloc] init];
  inner.orientation = NSUserInterfaceLayoutOrientationVertical;
  inner.alignment = NSLayoutAttributeLeading;
  inner.spacing = 6.0;
  inner.translatesAutoresizingMaskIntoConstraints = NO;

  NSTextField *header = kern_label([section[0] uppercaseString], 11.0,
                                   NSFontWeightBold,
                                   [NSColor colorWithWhite:0.58 alpha:1.0]);
  [inner addArrangedSubview:header];
  [inner setCustomSpacing:10.0 afterView:header];

  for (NSUInteger i = 1; i < section.count; i++) {
    NSArray *pair = section[i];
    NSView *r = kern_binding_row(pair[0], pair[1]);
    [inner addArrangedSubview:r];
    [r.widthAnchor constraintEqualToAnchor:inner.widthAnchor].active = YES;
  }
  return kern_card_wrap(inner);
}

/* A modifier explainer: a key-cap glyph next to its spelled-out name. */
static NSView *kern_mod_item(NSString *glyph, NSString *name) {
  NSStackView *s = [[NSStackView alloc] init];
  s.orientation = NSUserInterfaceLayoutOrientationHorizontal;
  s.spacing = 8.0;
  s.alignment = NSLayoutAttributeCenterY;
  s.translatesAutoresizingMaskIntoConstraints = NO;
  [s addArrangedSubview:[[KernKeycap alloc] initWithText:glyph]];
  [s addArrangedSubview:kern_label(name, 13.0, NSFontWeightRegular,
                                   [NSColor colorWithWhite:0.85 alpha:1.0])];
  return s;
}

/* The "how to read this" legend card: the four modifier keys, plus worked
   examples of keys pressed together vs. in sequence. */
static NSView *kern_legend_card(void) {
  NSStackView *mods = [[NSStackView alloc] init];
  mods.orientation = NSUserInterfaceLayoutOrientationHorizontal;
  mods.spacing = 26.0;
  mods.alignment = NSLayoutAttributeCenterY;
  mods.translatesAutoresizingMaskIntoConstraints = NO;
  [mods addArrangedSubview:kern_mod_item(@"⌃", @"Control")];
  [mods addArrangedSubview:kern_mod_item(@"⌥", @"Option")];
  [mods addArrangedSubview:kern_mod_item(@"⇧", @"Shift")];
  [mods addArrangedSubview:kern_mod_item(@"⌘", @"Command")];

  NSColor *dim = [NSColor colorWithWhite:0.6 alpha:1.0];
  NSStackView *ex = [[NSStackView alloc] init];
  ex.orientation = NSUserInterfaceLayoutOrientationHorizontal;
  ex.spacing = 10.0;
  ex.alignment = NSLayoutAttributeCenterY;
  ex.translatesAutoresizingMaskIntoConstraints = NO;
  [ex addArrangedSubview:kern_label(@"Keys touching", 13.0, NSFontWeightRegular, dim)];
  [ex addArrangedSubview:kern_chord_view(@"C-c")];
  [ex addArrangedSubview:kern_label(@"are pressed at the same time.", 13.0,
                                    NSFontWeightRegular, dim)];
  NSTextField *gap = kern_label(@"      ", 13.0, NSFontWeightRegular, dim);
  [ex addArrangedSubview:gap];
  [ex addArrangedSubview:kern_chord_view(@"C-x C-s")];
  [ex addArrangedSubview:kern_label(@"means press each group one after another.",
                                    13.0, NSFontWeightRegular, dim)];

  NSStackView *inner = [[NSStackView alloc] init];
  inner.orientation = NSUserInterfaceLayoutOrientationVertical;
  inner.alignment = NSLayoutAttributeLeading;
  inner.spacing = 14.0;
  inner.translatesAutoresizingMaskIntoConstraints = NO;
  [inner addArrangedSubview:mods];
  [inner addArrangedSubview:ex];
  return kern_card_wrap(inner);
}

- (void)showShortcuts:(id)sender {
  NSWindow *parent = [sender isKindOfClass:[NSView class]] ? [sender window] : nil;
  if (!parent) return;
  if (g_shortcuts_panel) {  /* already open — just bring it forward */
    [g_shortcuts_panel makeKeyAndOrderFront:nil];
    return;
  }

  /* ---- content data: section title, then {desc, chord-spec} pairs ---- */
  NSArray *sections = @[
    @[ @"Moving the cursor",
       @[@"Start of line", @"C-a"], @[@"End of line", @"C-e"],
       @[@"Forward a character", @"C-f"], @[@"Back a character", @"C-b"],
       @[@"Next line", @"C-n"], @[@"Previous line", @"C-p"],
       @[@"Forward a word", @"M-f"], @[@"Back a word", @"M-b"],
       @[@"Top of document", @"M-S-, / Cmd-S-,"],
       @[@"Bottom of document", @"M-S-. / Cmd-S-."],
       @[@"Go to line…", @"M-g"],
    ],
    @[ @"Scrolling",
       @[@"Page down", @"C-v"], @[@"Page up", @"M-v"],
       @[@"Recenter the view", @"C-l"],
    ],
    @[ @"Editing",
       @[@"Delete character ahead", @"C-d"], @[@"Delete character behind", @"Backspace"],
       @[@"Cut to end of line", @"C-k"], @[@"Delete word ahead", @"M-d"],
       @[@"Delete word behind", @"M-Backspace"], @[@"Insert a blank line", @"C-o"],
       @[@"Swap the two characters around the cursor", @"C-t"], @[@"Undo", @"C-/"],
       @[@"UPPERCASE word", @"M-u"], @[@"lowercase word", @"M-l"],
       @[@"Capitalize Word", @"M-c"],
       @[@"Indent list item", @"Tab"], @[@"Outdent list item", @"S-Tab"],
    ],
    @[ @"Selecting & the clipboard",
       @[@"Start selecting (set mark)", @"C-Space"],
       @[@"Extend selection while moving", @"S-Arrows"],
       @[@"Copy selection", @"M-w / Cmd-C"],
       @[@"Cut selection", @"C-w"], @[@"Paste", @"C-y / Cmd-V"],
       @[@"Select the whole document", @"C-x h"],
       @[@"Jump between selection ends", @"C-x C-x"],
       @[@"Cancel / clear selection", @"C-g"],
    ],
    @[ @"Formatting the selection",
       @[@"Bold", @"**"], @[@"Italic", @"*"],
       @[@"Highlight", @"=="], @[@"Inline code", @"`"],
    ],
    @[ @"Searching",
       @[@"Search forward", @"C-s"], @[@"Search backward", @"C-r"],
       @[@"Next match (while searching)", @"C-s"],
       @[@"Previous match (while searching)", @"C-r"],
       @[@"Finish searching", @"Return / Esc"],
       @[@"Cancel searching", @"C-g"],
    ],
    @[ @"Files",
       @[@"Save", @"C-x C-s"], @[@"Save as…", @"C-x C-w"],
       @[@"Open a file", @"C-x C-f"], @[@"Switch to a recent file", @"C-x b"],
       @[@"Quit", @"C-x C-c"],
    ],
    @[ @"Notes & links",
       @[@"Follow link under cursor", @"Cmd-Return"],
       @[@"Autocomplete a link", @"[["],
       @[@"Extract selection to a new note", @"Cmd-S-N"],
       @[@"Today's note", @"Cmd-S-T"],
       @[@"Go back", @"Cmd-S-Left"], @[@"Go forward", @"Cmd-S-Right"],
    ],
    @[ @"Display",
       @[@"Bigger text", @"Cmd-="], @[@"Smaller text", @"Cmd--"],
       @[@"Typewriter mode", @"C-x t"],
       @[@"Syntax highlighting", @"C-x y"],
       @[@"Style check", @"C-x s"],
       @[@"Symbols (ligatures)", @"C-x l"],
    ],
  ];

  NSColor *bg = [NSColor colorWithRed:30.0/255.0 green:30.0/255.0
                                 blue:32.0/255.0 alpha:1.0];

  /* groups balanced across three bento columns (indices into `sections`)
     0 Moving · 1 Scrolling · 2 Editing · 3 Selecting · 4 Formatting
     5 Searching · 6 Files · 7 Notes & links · 8 Display */
  NSArray *columnPlan = @[ @[@2, @3], @[@0, @5, @4], @[@7, @8, @6, @1] ];

  NSStackView *columns = [[NSStackView alloc] init];
  columns.orientation = NSUserInterfaceLayoutOrientationHorizontal;
  columns.distribution = NSStackViewDistributionFillEqually;
  columns.alignment = NSLayoutAttributeTop;
  columns.spacing = 14.0;
  columns.translatesAutoresizingMaskIntoConstraints = NO;

  for (NSArray *colGroups in columnPlan) {
    NSStackView *col = [[NSStackView alloc] init];
    col.orientation = NSUserInterfaceLayoutOrientationVertical;
    col.alignment = NSLayoutAttributeLeading;
    col.spacing = 14.0;
    col.translatesAutoresizingMaskIntoConstraints = NO;
    for (NSNumber *idx in colGroups) {
      NSView *card = kern_group_card(sections[idx.unsignedIntegerValue]);
      [col addArrangedSubview:card];
      [card.widthAnchor constraintEqualToAnchor:col.widthAnchor].active = YES;
    }
    /* a flexible tail so cards hug the top instead of spreading out when the
       column is shorter than its neighbours */
    NSView *tail = [[NSView alloc] init];
    tail.translatesAutoresizingMaskIntoConstraints = NO;
    [tail setContentHuggingPriority:1
                     forOrientation:NSLayoutConstraintOrientationVertical];
    [col addArrangedSubview:tail];
    [columns addArrangedSubview:col];
  }

  /* document stack: legend card + the three columns */
  NSStackView *doc = [[NSStackView alloc] init];
  doc.orientation = NSUserInterfaceLayoutOrientationVertical;
  doc.alignment = NSLayoutAttributeLeading;
  doc.spacing = 14.0;
  doc.edgeInsets = NSEdgeInsetsMake(22, 26, 26, 26);
  doc.translatesAutoresizingMaskIntoConstraints = NO;

  NSView *legend = kern_legend_card();
  [doc addArrangedSubview:legend];
  [doc addArrangedSubview:columns];
  CGFloat hInset = doc.edgeInsets.left + doc.edgeInsets.right;
  [legend.widthAnchor constraintEqualToAnchor:doc.widthAnchor constant:-hInset].active = YES;
  [columns.widthAnchor constraintEqualToAnchor:doc.widthAnchor constant:-hInset].active = YES;

  /* opaque container — opaque so AppKit does not recomposite the window against
     the live GL view beneath it. */
  KernFlippedView *container = [[KernFlippedView alloc] init];
  container.translatesAutoresizingMaskIntoConstraints = NO;
  container.wantsLayer = YES;
  container.layer.backgroundColor = bg.CGColor;
  [container addSubview:doc];
  [NSLayoutConstraint activateConstraints:@[
    [doc.topAnchor constraintEqualToAnchor:container.topAnchor],
    [doc.leadingAnchor constraintEqualToAnchor:container.leadingAnchor],
    [doc.trailingAnchor constraintEqualToAnchor:container.trailingAnchor],
    [doc.bottomAnchor constraintEqualToAnchor:container.bottomAnchor],
  ]];

  NSScrollView *scroll = [[NSScrollView alloc] init];
  scroll.translatesAutoresizingMaskIntoConstraints = NO;
  scroll.hasVerticalScroller = YES;
  scroll.drawsBackground = YES;
  scroll.backgroundColor = bg;
  scroll.documentView = container;
  [container.widthAnchor constraintEqualToAnchor:scroll.contentView.widthAnchor].active = YES;

  /* Done button row at the bottom */
  NSButton *done = [NSButton buttonWithTitle:@"Done"
                                      target:self
                                      action:@selector(closeShortcuts:)];
  done.keyEquivalent = @"\r";  /* Return dismisses */
  done.translatesAutoresizingMaskIntoConstraints = NO;

  NSView *content = [[NSView alloc] init];
  content.wantsLayer = YES;
  content.layer.backgroundColor = bg.CGColor;
  [content addSubview:scroll];
  [content addSubview:done];
  [NSLayoutConstraint activateConstraints:@[
    [scroll.topAnchor constraintEqualToAnchor:content.topAnchor],
    [scroll.leadingAnchor constraintEqualToAnchor:content.leadingAnchor],
    [scroll.trailingAnchor constraintEqualToAnchor:content.trailingAnchor],
    [done.topAnchor constraintEqualToAnchor:scroll.bottomAnchor constant:12],
    [done.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-16],
    [done.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-22],
  ]];

  /* Choose a width up to 80% of the window, then size the height to fit the
     content (capped at 90% of the window) so scrolling is rarely needed. */
  NSRect pf = parent.frame;
  CGFloat width = floor(pf.size.width * 0.8);
  if (width < 820.0)  width  = 820.0;
  if (width > 1280.0) width  = 1280.0;
  NSLayoutConstraint *widthC =
      [content.widthAnchor constraintEqualToConstant:width];
  widthC.active = YES;

  [content layoutSubtreeIfNeeded];
  CGFloat docH = container.fittingSize.height;
  CGFloat barH = 16.0 + done.fittingSize.height + 12.0;  /* Done button strip */
  CGFloat height = docH + barH;
  CGFloat capH = floor(pf.size.height * 0.9);
  if (height > capH) height = capH;
  NSLayoutConstraint *heightC =
      [content.heightAnchor constraintEqualToConstant:height];
  heightC.active = YES;

  NSPanel *panel = [[NSPanel alloc]
      initWithContentRect:NSMakeRect(0, 0, width, height)
                styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable)
                  backing:NSBackingStoreBuffered
                    defer:NO];
  panel.title = @"Keyboard Shortcuts";
  panel.opaque = YES;
  panel.backgroundColor = bg;
  panel.contentView = content;
  panel.delegate = self;
  panel.hidesOnDeactivate = NO;
  g_shortcuts_panel = panel;

  /* centre over the editor window, then show as an ordinary child window — not
     a modal sheet — so it never blocks the app from quitting. */
  NSRect wf = parent.frame;
  NSPoint origin = NSMakePoint(wf.origin.x + (wf.size.width - width) / 2.0,
                               wf.origin.y + (wf.size.height - height) / 2.0);
  [panel setFrameOrigin:origin];
  [parent addChildWindow:panel ordered:NSWindowAbove];
  [panel makeKeyAndOrderFront:nil];
}

- (void)dismissShortcuts {
  NSPanel *panel = g_shortcuts_panel;
  if (!panel) return;
  g_shortcuts_panel = nil;
  panel.delegate = nil;
  [panel.parentWindow removeChildWindow:panel];
  [panel orderOut:nil];
}

- (void)closeShortcuts:(id)sender {
  (void)sender;
  [self dismissShortcuts];
}

- (void)windowWillClose:(NSNotification *)note {
  (void)note;
  g_shortcuts_panel = nil;
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
  NSButton *helpBtn = kern_titlebar_button(@"info.circle", @"Keyboard shortcuts",
                                           @"Keyboard shortcuts",
                                           @selector(showShortcuts:),
                                           NSZeroRect);
  NSButton *folder = kern_titlebar_button(@"folder", @"Open documents folder",
                                          @"Open documents folder in Finder",
                                          @selector(openDocsFolder:),
                                          NSZeroRect);
  g_publish_btn = kern_titlebar_button(@"paperplane", @"Publish to X",
                                       @"Publish this note to X",
                                       @selector(publishToX:),
                                       NSZeroRect);
  g_publish_btn.hidden = (kern_x_is_connected() == 0);   /* until/unless connected */

  /* AppKit stretches the accessory view to the full (toolbar-tall) title bar
     height. Pin the buttons to its vertical center via Auto Layout so they line
     up with the window title instead of sinking to the bottom edge. The three
     buttons are pinned from the *right* (folder, then help, then publish to its
     left); when the publish button is hidden its empty slot falls on the inner
     side against the title bar, so there's no visible gap. */
  NSView *buttons = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 120, 30)];
  helpBtn.translatesAutoresizingMaskIntoConstraints = NO;
  folder.translatesAutoresizingMaskIntoConstraints = NO;
  g_publish_btn.translatesAutoresizingMaskIntoConstraints = NO;
  [buttons addSubview:helpBtn];
  [buttons addSubview:folder];
  [buttons addSubview:g_publish_btn];
  [NSLayoutConstraint activateConstraints:@[
    [helpBtn.widthAnchor constraintEqualToConstant:32],
    [helpBtn.heightAnchor constraintEqualToConstant:24],
    [folder.widthAnchor constraintEqualToConstant:32],
    [folder.heightAnchor constraintEqualToConstant:24],
    [g_publish_btn.widthAnchor constraintEqualToConstant:32],
    [g_publish_btn.heightAnchor constraintEqualToConstant:24],
    [folder.trailingAnchor constraintEqualToAnchor:buttons.trailingAnchor constant:-8],
    [helpBtn.trailingAnchor constraintEqualToAnchor:folder.leadingAnchor constant:-6],
    [g_publish_btn.trailingAnchor constraintEqualToAnchor:helpBtn.leadingAnchor constant:-6],
    [helpBtn.centerYAnchor constraintEqualToAnchor:buttons.centerYAnchor],
    [folder.centerYAnchor constraintEqualToAnchor:buttons.centerYAnchor],
    [g_publish_btn.centerYAnchor constraintEqualToAnchor:buttons.centerYAnchor],
  ]];

  NSTitlebarAccessoryViewController *acc = [[NSTitlebarAccessoryViewController alloc] init];
  acc.layoutAttribute = NSLayoutAttributeRight;
  acc.view = buttons;
  [nswindow addTitlebarAccessoryViewController:acc];

  kern_install_view_menu_checkmarks();   /* live ✓ on the View-menu toggles */
}
