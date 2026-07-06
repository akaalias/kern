//
//  KernApp.swift
//  Kern
//
//  Created by Alexis Rondeau on 18.06.26.
//
//  NOTE on threading: editor_main() runs SDL's own blocking event loop and never
//  returns, so the main thread / main actor is parked for the life of the app.
//  That means NOTHING here may depend on the main actor after launch —
//  DispatchQueue.main.async never drains, main-actor Tasks never run, and SwiftUI
//  can't live-update. The X integration therefore runs entirely off the main
//  actor and reports progress through the C status bar (kern_x_set_status), which
//  the SDL render loop paints. The SwiftUI Settings tab only reads state when it
//  is (re)drawn.
//

import SwiftUI
import CryptoKit
import Network
import UserNotifications
import ImageIO
import CoreGraphics

class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationDidFinishLaunching(_ notification: Notification) {
        // Point the editor's file I/O at the app's sandbox-container Documents
        // folder (~/Library/Containers/<id>/Data/Documents), creating it if needed.
        let fm = FileManager.default
        if let docs = fm.urls(for: .documentDirectory, in: .userDomainMask).first {
            try? fm.createDirectory(at: docs, withIntermediateDirectories: true)
            docs.path.withCString { editor_set_documents_dir($0) }
        }

        // Preload the X account link from the Keychain so the Settings tab
        // reflects it the first time it's opened.
        _ = XAuth.shared.isConnected

        // Ask once (now, before the main thread is parked) for permission to post
        // banners. The completion runs on a background queue, so it's safe; if the
        // user denies, publish results still land in the editor's status bar.
        UNUserNotificationCenter.current()
            .requestAuthorization(options: [.alert, .sound]) { _, _ in }

        // Hand off to the C editor once the app is a proper foreground app.
        // editor_main runs SDL's own window + event loop (blocking); the
        // SwiftUI side is just the launcher shell.
        DispatchQueue.main.async {
            editor_main(0, nil)
        }
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        return true
    }
}

@main
struct KernApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) var appDelegate

    var body: some Scene {
        // No WindowGroup: the editor runs in its own SDL window. The Settings
        // scene (⌘,) hosts our preferences, including the X / Twitter tab.
        Settings {
            SettingsView()
        }
        // Let the Settings window hug each tab's content size (it animates between
        // the small X tab and the wide Keyboard Shortcuts tab).
        .windowResizability(.contentSize)
        // Menu-bar View toggles for the syntax/style overlays. Defining them
        // through SwiftUI's `.commands` (rather than poking NSApp.mainMenu, which
        // SwiftUI rebuilds) is what makes them stick. The actions call straight
        // into the C editor — menu selection runs synchronously on the main
        // thread during tracking, so it works despite the parked run loop.
        //
        // Each master (Syntax Highlighting / Style Check) flips everything in its
        // group; the per-type items flip a single class/category. Live checkmarks
        // can't come from SwiftUI (the parked main actor blocks updates) — an
        // AppKit NSMenuDelegate sets them from the C state (Platform/macos_style.m),
        // matching items by these exact titles, so keep the two in sync.
        // The full command set as standard menus. Every item calls a kern_menu_*
        // bridge that replays the exact keyboard chord through the editor's
        // event path. Items carry NO SwiftUI keyboardShortcuts on purpose:
        // AppKit key equivalents would intercept keys before the SDL loop sees
        // them (risky with the parked main actor). Instead the chord is drawn
        // as styled text by the menu delegate in Platform/macos_style.m —
        // titles here must match its spec tables EXACTLY.
        .commands {
            CommandGroup(replacing: .newItem) {
                Button("Open…") { kern_menu_open() }
                Button("Save") { kern_menu_save() }
                Button("Save As…") { kern_menu_save_as() }
                Divider()
                Button("Switch to Recent Buffer") { kern_menu_switch_buffer() }
                Button("Today's Note") { kern_menu_daily_note() }
            }
            CommandGroup(replacing: .undoRedo) {
                Button("Undo") { kern_menu_undo() }
            }
            CommandGroup(replacing: .pasteboard) {
                Button("Cut") { kern_menu_cut() }
                Button("Copy") { kern_menu_copy() }
                Button("Paste") { kern_menu_paste() }
                Button("Select All") { kern_menu_select_all() }
                Divider()
                Button("Kill to End of Line") { kern_menu_kill_line() }
                Button("Delete Word Forward") { kern_menu_delete_word_fwd() }
                Button("Delete Word Backward") { kern_menu_delete_word_back() }
                Button("Transpose Characters") { kern_menu_transpose() }
                Button("Insert Blank Line") { kern_menu_open_line() }
                Divider()
                Button("UPPERCASE Word") { kern_menu_upcase() }
                Button("lowercase Word") { kern_menu_downcase() }
                Button("Capitalize Word") { kern_menu_capitalize() }
                Divider()
                Button("Search Forward") { kern_menu_search_fwd() }
                Button("Search Backward") { kern_menu_search_back() }
            }
            CommandMenu("Format") {
                Button("Bold") { kern_menu_bold() }
                Button("Italic") { kern_menu_italic() }
                Button("Highlight") { kern_menu_highlight() }
                Button("Underline") { kern_menu_underline() }
                Button("Inline Code") { kern_menu_code() }
                Divider()
                Button("Highlight Sentence") { kern_menu_sentence_highlight() }
                Button("Underline Sentence") { kern_menu_sentence_underline() }
                Divider()
                Button("Indent List Item") { kern_menu_indent() }
                Button("Outdent List Item") { kern_menu_outdent() }
            }
            CommandGroup(after: .toolbar) {
                Button("Typewriter Mode") { kern_menu_typewriter() }
                Button("Symbols") { kern_toggle_subs() }
                Button("Page Borders") { kern_menu_page_borders() }
                Divider()
                Button("Syntax Highlighting") { kern_toggle_syntax() }
                Button("Verbs") { kern_toggle_verbs() }
                Button("Nouns") { kern_toggle_nouns() }
                Button("Adjectives") { kern_toggle_adjectives() }
                Button("Adverbs") { kern_toggle_adverbs() }
                Button("Function Words") { kern_toggle_function_words() }
                Divider()
                Button("Style Check") { kern_toggle_style() }
                Button("Fillers") { kern_toggle_fillers() }
                Button("Cliches") { kern_toggle_cliches() }
                Button("Redundancies") { kern_toggle_redundancies() }
                Divider()
                Button("Bigger Text") { kern_menu_font_bigger() }
                Button("Smaller Text") { kern_menu_font_smaller() }
                Button("Recenter") { kern_menu_recenter() }
                Button("Page Down") { kern_menu_page_down() }
                Button("Page Up") { kern_menu_page_up() }
            }
            CommandMenu("Go") {
                Button("Top of Document") { kern_menu_top() }
                Button("Bottom of Document") { kern_menu_bottom() }
                Button("Go to Line…") { kern_menu_goto_line() }
                Divider()
                Button("Follow Link") { kern_menu_follow_link() }
                Button("Back") { kern_menu_back() }
                Button("Forward") { kern_menu_forward() }
            }
            CommandMenu("Notes") {
                Button("Extract Selection to New Note") { kern_menu_extract_note() }
                Button("Margin Note") { kern_menu_margin_note() }
                Divider()
                Button("Publish to X…") { kern_publish_to_x() }
                Button("Download News Feed") { kern_menu_fetch_news() }
                Button("Download Bookmarks") { kern_menu_fetch_bookmarks() }
            }
            // Window menu: open the documents folder in Finder (replaces the
            // former title-bar folder button).
            CommandGroup(after: .windowArrangement) {
                Button("Open Documents Folder in Finder") { kern_open_documents_folder() }
            }
        }
    }
}

// MARK: - Settings UI

struct SettingsView: View {
    var body: some View {
        TabView {
            XSettingsView()
                .padding(20)
                .frame(width: 460, height: 320)
                // tabItem must carry an SF Symbol — a Label with a systemImage is
                // what makes macOS render the native preferences toolbar tab bar
                // (text-only tabItems fall back to the ugly segmented control).
                .tabItem { Label("X (Twitter)", systemImage: "paperplane") }

            ShortcutsView()
                .tabItem { Label("Keyboard Shortcuts", systemImage: "keyboard") }
        }
    }
}

// MARK: - Keyboard Shortcuts (native sidebar + detail)

struct ShortcutBinding: Identifiable {
    let id = UUID()
    let desc: String
    let spec: String   // emacs-style chord spec, e.g. "C-x C-s", "M-w / Cmd-C"
}

struct ShortcutGroup: Identifiable {
    let id = UUID()
    let title: String
    let bindings: [ShortcutBinding]
}

private func B(_ d: String, _ s: String) -> ShortcutBinding { ShortcutBinding(desc: d, spec: s) }

let shortcutGroups: [ShortcutGroup] = [
    ShortcutGroup(title: "Moving the cursor", bindings: [
        B("Start of line", "C-a"), B("End of line", "C-e"),
        B("Forward a character", "C-f"), B("Back a character", "C-b"),
        B("Next line", "C-n"), B("Previous line", "C-p"),
        B("Forward a word", "M-f"), B("Back a word", "M-b"),
        B("Jump by word (arrow keys)", "C-Arrows / M-Arrows"),
        B("Top of document", "M-S-, / Cmd-S-,"),
        B("Bottom of document", "M-S-. / Cmd-S-."),
        B("Go to line…", "M-g"),
    ]),
    ShortcutGroup(title: "Editing", bindings: [
        B("Delete character ahead", "C-d"), B("Delete character behind", "Backspace"),
        B("Cut to end of line", "C-k"), B("Delete word ahead", "M-d"),
        B("Delete word behind", "M-Backspace"), B("Insert a blank line", "C-o"),
        B("Swap the two characters around the cursor", "C-t"), B("Undo", "C-/"),
        B("UPPERCASE word", "M-u"), B("lowercase word", "M-l"),
        B("Capitalize Word", "M-c"),
        B("Indent list item", "Tab"), B("Outdent list item", "S-Tab"),
    ]),
    ShortcutGroup(title: "Selecting & the clipboard", bindings: [
        B("Start selecting (set mark)", "C-Space"),
        B("Extend selection while moving", "S-Arrows"),
        B("Copy selection", "M-w / Cmd-C"),
        B("Cut selection", "C-w"), B("Paste", "C-y / Cmd-V"),
        B("Delete the selected region", "Backspace / Delete"),
        B("Select the whole document", "C-x h"),
        B("Jump between selection ends", "C-x C-x"),
        B("Cancel / clear selection", "C-g"),
    ]),
    ShortcutGroup(title: "Formatting", bindings: [
        B("Bold selection", "**"), B("Italic selection", "*"),
        B("Highlight selection", "=="), B("Underline selection", "++"),
        B("Inline code selection", "`"),
        B("Highlight the current sentence", "Cmd-S-H"),
        B("Underline the current sentence", "Cmd-S-U"),
    ]),
    ShortcutGroup(title: "Searching", bindings: [
        B("Search forward", "C-s"), B("Search backward", "C-r"),
        B("Next match (while searching)", "C-s"),
        B("Previous match (while searching)", "C-r"),
        B("Finish searching", "Return / Esc"),
        B("Cancel searching", "C-g"),
    ]),
    ShortcutGroup(title: "Notes & links", bindings: [
        B("Follow link under cursor", "Cmd-Return"),
        B("Autocomplete a link", "[["),
        B("Extract selection to a new note", "Cmd-S-N"),
        B("Margin note at the caret", "Cmd-S-M"),
        B("Today's note", "Cmd-S-T"),
        B("Go back", "Cmd-S-Left"), B("Go forward", "Cmd-S-Right"),
    ]),
    ShortcutGroup(title: "X (Twitter)", bindings: [
        B("Download your home news feed", "C-x n"),
        B("Download all your bookmarks", "C-x m"),
        B("Publish (from the title-bar button): confirm", "Return"),
        B("Publish: cancel", "Esc"),
    ]),
    ShortcutGroup(title: "Files", bindings: [
        B("Save", "C-x C-s"), B("Save as…", "C-x C-w"),
        B("Open a file", "C-x C-f"), B("Switch to a recent file", "C-x b"),
        B("Quit", "C-x C-c"),
    ]),
    ShortcutGroup(title: "Display", bindings: [
        B("Bigger text", "Cmd-="), B("Smaller text", "Cmd--"),
        B("Typewriter mode", "C-x t"), B("Syntax highlighting", "C-x y"),
        B("Style check", "C-x s"), B("Symbols (ligatures)", "C-x l"),
        B("Page borders", "C-x p"),
    ]),
    ShortcutGroup(title: "Scrolling", bindings: [
        B("Page down", "C-v"), B("Page up", "M-v"),
        B("Recenter the view", "C-l"),
    ]),
]

/// A vertical-tab keyboard-shortcuts reference: a plain list of group "tabs" on
/// the left (not a NavigationSplitView app-sidebar — no collapse button, no
/// toolbar title, single-click selection), the chosen group's bindings on the
/// right.
struct ShortcutsView: View {
    @State private var selection: ShortcutGroup.ID = shortcutGroups[0].id

    var body: some View {
        HStack(spacing: 0) {
            // left: vertical tab list
            VStack(alignment: .leading, spacing: 2) {
                ForEach(shortcutGroups) { group in
                    Button {
                        selection = group.id
                    } label: {
                        Text(group.title)
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .padding(.horizontal, 10)
                            .padding(.vertical, 6)
                            .background(
                                RoundedRectangle(cornerRadius: 6)
                                    .fill(selection == group.id ? Color.primary.opacity(0.12) : .clear)
                            )
                            .contentShape(Rectangle())
                    }
                    .buttonStyle(.plain)
                }
                Spacer()
            }
            .padding(10)
            .frame(width: 220)

            Divider()

            // right: the selected group's bindings
            ShortcutGroupDetail(group: shortcutGroups.first { $0.id == selection } ?? shortcutGroups[0])
                .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
        .frame(width: 760, height: 660)
    }
}

private struct ShortcutGroupDetail: View {
    let group: ShortcutGroup
    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 0) {
                Text(group.title)
                    .font(.headline)
                    .padding(.bottom, 8)
                ForEach(Array(group.bindings.enumerated()), id: \.element.id) { i, b in
                    HStack(alignment: .center) {
                        Text(b.desc)
                        Spacer(minLength: 16)
                        ChordView(spec: b.spec)
                    }
                    .padding(.vertical, 7)
                    if i < group.bindings.count - 1 { Divider().opacity(0.4) }
                }
            }
            .padding(22)
            .frame(maxWidth: .infinity, alignment: .leading)
        }
    }
}

// One rounded key-cap.
private struct KeyCap: View {
    let text: String
    var body: some View {
        Text(text)
            .font(.system(size: 12.5, weight: .medium))
            .padding(.horizontal, 7).padding(.vertical, 3)
            .frame(minWidth: 22)
            .background(RoundedRectangle(cornerRadius: 5).fill(Color.primary.opacity(0.08)))
            .overlay(RoundedRectangle(cornerRadius: 5).stroke(Color.primary.opacity(0.18)))
            .fixedSize()
    }
}

// Renders an emacs-style chord spec into key-caps. " / " = alternatives ("or"),
// a space = chords pressed in sequence ("then"), modifier prefixes (C-/M-/S-/Cmd-)
// become ⌃/⌥/⇧/⌘ caps.
private struct ChordView: View {
    let spec: String

    private enum Element {
        case cap(String), sep(String)
    }

    private static func caps(for chord: String) -> [String] {
        var s = chord
        var out: [String] = []
        let mods: [(String, String)] = [("Cmd-", "⌘"), ("C-", "⌃"), ("M-", "⌥"), ("S-", "⇧")]
        var changed = true
        while changed {
            changed = false
            for (prefix, sym) in mods where s.hasPrefix(prefix) {
                out.append(sym); s.removeFirst(prefix.count); changed = true
            }
        }
        if !s.isEmpty {
            out.append(s.count == 1 && s.first!.isLetter ? s.uppercased() : s)
        }
        return out
    }

    private var elements: [Element] {
        var out: [Element] = []
        for (ai, alt) in spec.components(separatedBy: " / ").enumerated() {
            if ai > 0 { out.append(.sep("or")) }
            for (ci, chord) in alt.split(separator: " ").map(String.init).enumerated() {
                if ci > 0 { out.append(.sep("then")) }
                for cap in Self.caps(for: chord) { out.append(.cap(cap)) }
            }
        }
        return out
    }

    var body: some View {
        HStack(spacing: 5) {
            ForEach(Array(elements.enumerated()), id: \.offset) { _, el in
                switch el {
                case .cap(let s): KeyCap(text: s)
                case .sep(let s): Text(s).font(.system(size: 11)).foregroundStyle(.secondary)
                }
            }
        }
    }
}

struct XSettingsView: View {
    // The connection state lives in XAuth.shared and can't be observed (the main
    // actor is parked, so no live updates). We read it fresh every time `body`
    // is evaluated; `tick` forces a re-evaluation on appear and on button taps.
    @State private var tick = 0
    @State private var hint: String?

    var body: some View {
        let _ = tick
        let connected = XAuth.shared.isConnected
        let handle = XAuth.shared.username

        return VStack(alignment: .leading, spacing: 16) {
            Text("X (Twitter)")
                .font(.title2).bold()

            Text("Publish the note you're viewing to your X timeline with the "
                 + "Publish button in the title bar, and pull X into Kern: "
                 + "⌃X N downloads your home feed, ⌃X M all your bookmarks. "
                 + "Connect once; Kern keeps you signed in.")
                .font(.callout)
                .foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)

            Divider()

            HStack(spacing: 10) {
                Circle()
                    .fill(connected ? Color.green : Color.secondary)
                    .frame(width: 10, height: 10)
                if connected {
                    Text("Connected as ") + Text(handle.map { "@\($0)" } ?? "your account").bold()
                } else {
                    Text("Not connected")
                }
                Spacer()
            }

            HStack {
                if connected {
                    Button("Disconnect", role: .destructive) {
                        XAuth.shared.disconnect()
                        hint = nil
                        tick += 1
                    }
                } else {
                    Button("Connect X Account") {
                        XAuth.shared.startConnect()
                        hint = "Approve in your browser. Watch the editor's status bar — it'll say "
                             + "“Connected to X as …”, then hit Refresh."
                        tick += 1
                    }
                }
                Button("Refresh") {
                    XAuth.shared.reload()
                    tick += 1
                }
            }

            if let hint {
                Text(hint)
                    .font(.footnote)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }

            Spacer(minLength: 0)
        }
        // Re-read XAuth state whenever the tab/window is shown again.
        .onAppear { tick += 1 }
    }
}

/// Show a transient message in the editor's status bar (the only UI channel that
/// works while the main thread is parked in the SDL loop).
nonisolated func reportXStatus(_ s: String) {
    s.withCString { kern_x_set_status($0) }
}

/// Post a native macOS banner (Notification Center). Safe from any thread —
/// UNUserNotificationCenter is thread-safe, so this sidesteps the parked main
/// actor. Silently no-ops if the user declined notification permission.
nonisolated func notifyX(_ title: String, _ body: String) {
    let content = UNMutableNotificationContent()
    content.title = title
    content.body = body
    content.sound = .default
    let req = UNNotificationRequest(identifier: UUID().uuidString,
                                    content: content, trigger: nil)
    UNUserNotificationCenter.current().add(req)
}

// MARK: - OAuth 2.0 + posting

/// Owns the X account link: the one-time PKCE consent, token storage/refresh in
/// the Keychain, and the actual POST /2/tweets call. Deliberately `nonisolated`
/// and lock-guarded — every entry point runs on a background thread.
nonisolated final class XAuth {
    static let shared = XAuth()

    // Public, native (PKCE-only) client — no secret. Injected at build time from
    // Config/Secrets.xcconfig -> Info.plist (XClientID) so it stays out of the
    // repo. Empty when unconfigured; connect()/post() guard on that.
    private let clientID = (Bundle.main.object(forInfoDictionaryKey: "XClientID") as? String) ?? ""
    // Loopback redirect: the browser sends the code back to a local listener.
    // This must exactly match a Callback URI registered in the X portal.
    private let loopbackPort: UInt16 = 8123
    private var redirectURI: String { "http://127.0.0.1:\(loopbackPort)/callback" }
    // bookmark.read gates GET /2/users/:id/bookmarks (C-x m). Accounts
    // connected before it was added must disconnect + reconnect to grant it.
    private let scopes = "tweet.read tweet.write users.read bookmark.read offline.access"

    private let authorizeURL = "https://x.com/i/oauth2/authorize"
    private let tokenURL = "https://api.x.com/2/oauth2/token"
    private let tweetsURL = "https://api.x.com/2/tweets"
    private let meURL = "https://api.x.com/2/users/me"
    private let usersURL = "https://api.x.com/2/users"   // …/{id}/timelines/reverse_chronological

    private let lock = NSLock()
    private var _tokens: XTokens?

    private var tokens: XTokens? {
        get { lock.lock(); defer { lock.unlock() }; return _tokens }
        set { lock.lock(); _tokens = newValue; lock.unlock() }
    }

    var isConnected: Bool { tokens != nil }
    var username: String? { tokens?.username }
    var displayName: String? { tokens?.name }

    // Decoded avatar RGBA, held in stable C-owned memory so kern_x_avatar_rgba
    // can hand the pointer straight to the GL renderer. Guarded by avatarLock.
    private let avatarLock = NSLock()
    private var avatarPtr: UnsafeMutablePointer<UInt8>?
    private var avatarW = 0, avatarH = 0

    private init() {
        _tokens = Keychain.loadTokens()
        loadAvatarIfNeeded()
    }

    /// Re-read tokens from the Keychain (for the Settings "Refresh" button).
    func reload() {
        tokens = Keychain.loadTokens()
        loadAvatarIfNeeded()
    }

    // MARK: avatar

    /// Kick off a background download+decode of the profile photo if we have a
    /// URL but no pixels yet. Safe to call repeatedly (no-op once loaded).
    func loadAvatarIfNeeded() {
        avatarLock.lock(); let have = avatarPtr != nil; avatarLock.unlock()
        guard !have, let urlStr = tokens?.avatarURL, let url = URL(string: urlStr) else { return }
        Task.detached { [self] in
            guard let (data, _) = try? await URLSession.shared.data(from: url),
                  let (bytes, w, h) = Self.decodeRGBA(data) else { return }
            avatarLock.lock()
            free(avatarPtr)
            let p = UnsafeMutablePointer<UInt8>.allocate(capacity: bytes.count)
            bytes.withUnsafeBufferPointer { p.update(from: $0.baseAddress!, count: bytes.count) }
            avatarPtr = p; avatarW = w; avatarH = h
            avatarLock.unlock()
        }
    }

    /// (pointer, width, height) of the decoded avatar, or nil if not loaded.
    func avatar() -> (UnsafePointer<UInt8>, Int, Int)? {
        avatarLock.lock(); defer { avatarLock.unlock() }
        guard let p = avatarPtr else { return nil }
        return (UnsafePointer(p), avatarW, avatarH)
    }

    /// Decode image bytes into tightly-packed straight-alpha RGBA (top row
    /// first), with an anti-aliased circular alpha baked in — so the renderer
    /// can draw it as a plain alpha-blended quad and, minified to ~48px, the
    /// circle edge comes out smooth (no 1-bit stencil stair-stepping).
    private static func decodeRGBA(_ data: Data) -> (bytes: [UInt8], w: Int, h: Int)? {
        guard let src = CGImageSourceCreateWithData(data as CFData, nil),
              let img = CGImageSourceCreateImageAtIndex(src, 0, nil) else { return nil }
        let w = img.width, h = img.height
        guard w > 0, h > 0 else { return nil }
        var bytes = [UInt8](repeating: 0, count: w * h * 4)
        let cs = CGColorSpaceCreateDeviceRGB()
        // noneSkipLast: opaque RGB, alpha byte unused — we fill alpha ourselves.
        let info = CGImageAlphaInfo.noneSkipLast.rawValue
        let ok = bytes.withUnsafeMutableBytes { buf -> Bool in
            guard let ctx = CGContext(data: buf.baseAddress, width: w, height: h,
                                      bitsPerComponent: 8, bytesPerRow: w * 4,
                                      space: cs, bitmapInfo: info) else { return false }
            ctx.draw(img, in: CGRect(x: 0, y: 0, width: w, height: h))
            return true
        }
        guard ok else { return nil }
        // Circular coverage mask with a 1px anti-aliased edge band.
        let cx = Double(w) / 2, cy = Double(h) / 2
        let r = Double(min(w, h)) / 2
        for y in 0..<h {
            for x in 0..<w {
                let dx = Double(x) + 0.5 - cx, dy = Double(y) + 0.5 - cy
                let cov = max(0.0, min(1.0, r - (dx * dx + dy * dy).squareRoot() + 0.5))
                bytes[(y * w + x) * 4 + 3] = UInt8(cov * 255.0)
            }
        }
        return (bytes, w, h)
    }

    // MARK: connect / disconnect

    /// Kick off the connect flow on a background task. Safe to call from the
    /// main actor (e.g. a SwiftUI button) — it returns immediately.
    func startConnect() {
        Task.detached { [self] in
            do { try await connect() }
            catch { reportXStatus("X: \(error.localizedDescription)") }
        }
    }

    private func connect() async throws {
        guard !clientID.isEmpty else {
            throw XError("X client ID not configured (Config/Secrets.xcconfig).")
        }
        let verifier = Self.randomURLSafe(64)
        let challenge = Self.codeChallenge(for: verifier)
        let state = Self.randomURLSafe(32)

        var comps = URLComponents(string: authorizeURL)!
        comps.queryItems = [
            .init(name: "response_type", value: "code"),
            .init(name: "client_id", value: clientID),
            .init(name: "redirect_uri", value: redirectURI),
            .init(name: "scope", value: scopes),
            .init(name: "state", value: state),
            .init(name: "code_challenge", value: challenge),
            .init(name: "code_challenge_method", value: "S256"),
        ]
        let authURL = comps.url!

        let server = LoopbackServer(port: loopbackPort)
        reportXStatus("Opening X sign-in in your browser…")
        let items = try await withThrowingTaskGroup(of: [URLQueryItem].self) { group in
            group.addTask { try await server.waitForCallback() }
            group.addTask {
                try await Task.sleep(nanoseconds: 180 * 1_000_000_000)  // 3-min cap
                throw XError("Timed out waiting for X authorization.")
            }
            defer { group.cancelAll(); server.stop() }
            _ = NSWorkspace.shared.open(authURL)
            guard let first = try await group.next() else { throw XError("Authorization failed.") }
            return first
        }

        guard items.first(where: { $0.name == "state" })?.value == state,
              let code = items.first(where: { $0.name == "code" })?.value
        else {
            let err = items.first(where: { $0.name == "error" })?.value
            throw XError(err.map { "X denied authorization: \($0)" } ?? "No authorization code received.")
        }

        reportXStatus("Finishing X sign-in…")
        var fresh = try await exchangeCode(code, verifier: verifier)
        if let p = try? await fetchProfile(accessToken: fresh.accessToken) {
            fresh.username = p.username; fresh.name = p.name; fresh.avatarURL = p.avatarURL
            fresh.userID = p.id
        }
        store(fresh)
        loadAvatarIfNeeded()
        reportXStatus("Connected to X as @\(fresh.username ?? "account") \u{2713}")
    }

    func disconnect() {
        Keychain.deleteTokens()
        tokens = nil
    }

    // MARK: posting

    /// Post `text` as a tweet, refreshing the access token first if needed.
    /// Returns the public URL of the created tweet.
    @discardableResult
    func post(text: String) async throws -> String {
        let token = try await validAccessToken()
        var req = URLRequest(url: URL(string: tweetsURL)!)
        req.httpMethod = "POST"
        req.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization")
        req.setValue("application/json", forHTTPHeaderField: "Content-Type")
        req.httpBody = try JSONSerialization.data(withJSONObject: ["text": text])

        let (data, resp) = try await URLSession.shared.data(for: req)
        let http = resp as? HTTPURLResponse
        guard http?.statusCode == 201 else {
            throw XError(Self.apiMessage(data) ?? "post failed (HTTP \(http?.statusCode ?? -1))")
        }
        let obj = try? JSONSerialization.jsonObject(with: data) as? [String: Any]
        let id = (obj?["data"] as? [String: Any])?["id"] as? String ?? ""
        let user = tokens?.username ?? "i"
        return "https://x.com/\(user)/status/\(id)"
    }

    // MARK: token plumbing

    private func validAccessToken() async throws -> String {
        guard let t = tokens else { throw XError("not connected") }
        // Refresh a minute early to absorb clock skew / request latency.
        if Date() < t.expiresAt.addingTimeInterval(-60) { return t.accessToken }
        return try await refresh(using: t.refreshToken)
    }

    private func exchangeCode(_ code: String, verifier: String) async throws -> XTokens {
        try await tokenRequest([
            "code": code,
            "grant_type": "authorization_code",
            "client_id": clientID,
            "redirect_uri": redirectURI,
            "code_verifier": verifier,
        ])
    }

    @discardableResult
    private func refresh(using refreshToken: String) async throws -> String {
        var fresh = try await tokenRequest([
            "refresh_token": refreshToken,
            "grant_type": "refresh_token",
            "client_id": clientID,
        ])
        // The token response omits profile fields; carry them forward.
        fresh.username = tokens?.username
        fresh.name = tokens?.name
        fresh.avatarURL = tokens?.avatarURL
        fresh.userID = tokens?.userID
        store(fresh)
        return fresh.accessToken
    }

    private func tokenRequest(_ form: [String: String]) async throws -> XTokens {
        var req = URLRequest(url: URL(string: tokenURL)!)
        req.httpMethod = "POST"
        req.setValue("application/x-www-form-urlencoded", forHTTPHeaderField: "Content-Type")
        req.httpBody = Self.formEncode(form).data(using: .utf8)

        let (data, resp) = try await URLSession.shared.data(for: req)
        let status = (resp as? HTTPURLResponse)?.statusCode ?? -1
        guard status == 200,
              let obj = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let access = obj["access_token"] as? String
        else {
            throw XError(Self.apiMessage(data) ?? "token request failed (HTTP \(status))")
        }

        let expiresIn = (obj["expires_in"] as? Double) ?? 7200
        // Refresh tokens rotate; fall back to the old one if X omits a new one.
        let refresh = (obj["refresh_token"] as? String) ?? tokens?.refreshToken ?? ""
        return XTokens(accessToken: access,
                       refreshToken: refresh,
                       expiresAt: Date().addingTimeInterval(expiresIn),
                       username: nil)
    }

    /// Fetch the connected account's handle, display name, avatar URL, and
    /// numeric user id from /2/users/me (the avatar is upscaled from X's
    /// default _normal 48px variant; the id is what the timeline endpoint keys on).
    private func fetchProfile(accessToken: String) async throws
        -> (username: String?, name: String?, avatarURL: String?, id: String?) {
        var comps = URLComponents(string: meURL)!
        comps.queryItems = [.init(name: "user.fields", value: "name,profile_image_url")]
        var req = URLRequest(url: comps.url!)
        req.setValue("Bearer \(accessToken)", forHTTPHeaderField: "Authorization")
        let (data, _) = try await URLSession.shared.data(for: req)
        let obj = try? JSONSerialization.jsonObject(with: data) as? [String: Any]
        let d = obj?["data"] as? [String: Any]
        var avatar = d?["profile_image_url"] as? String
        avatar = avatar?.replacingOccurrences(of: "_normal.", with: "_400x400.")
        return (d?["username"] as? String, d?["name"] as? String, avatar,
                d?["id"] as? String)
    }

    // MARK: home timeline (news feed)

    /// The account's numeric user id, resolved lazily: accounts connected before
    /// the news feature have no stored id, so fall back to /2/users/me once and
    /// persist it for next time.
    private func userID(token: String) async throws -> String {
        if let id = tokens?.userID, !id.isEmpty { return id }
        guard let id = try await fetchProfile(accessToken: token).id else {
            throw XError("couldn't resolve your X user id")
        }
        if var t = tokens { t.userID = id; store(t) }
        return id
    }

    /// Fetch the reverse-chronological home timeline and format it as markdown —
    /// one "## author — date — snippet" entry per post, with the full text,
    /// byline, and public URL below (the shape Kern's news note wants).
    func homeTimeline(maxResults: Int = 50) async throws -> String {
        let token = try await validAccessToken()
        let id = try await userID(token: token)
        var comps = URLComponents(string: "\(usersURL)/\(id)/timelines/reverse_chronological")!
        comps.queryItems = [
            .init(name: "max_results", value: "\(maxResults)"),
            .init(name: "tweet.fields", value: "created_at,author_id"),
            .init(name: "expansions", value: "author_id"),
            .init(name: "user.fields", value: "name,username"),
        ]
        var req = URLRequest(url: comps.url!)
        req.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization")
        let (data, resp) = try await URLSession.shared.data(for: req)
        let status = (resp as? HTTPURLResponse)?.statusCode ?? -1
        guard status == 200 else {
            throw XError(Self.apiMessage(data) ?? "feed fetch failed (HTTP \(status))")
        }
        return Self.feedMarkdown(data)
    }

    /// Fetch ALL bookmarks — GET /2/users/:id/bookmarks, paginated with
    /// pagination_token until meta.next_token runs out — formatted like the
    /// news feed but with NOTHING filtered: every bookmark was marked
    /// deliberately, so link-only posts and one-liners stay in. If a later
    /// page fails after some were collected (e.g. a rate limit), the partial
    /// result is returned with a note rather than thrown away.
    func bookmarks() async throws -> String {
        let token = try await validAccessToken()
        let id = try await userID(token: token)
        var out = ""
        var pageToken: String? = nil
        for page in 1...80 {                    // safety cap: 80 pages = 4,000 bookmarks
            reportXStatus("Fetching your X bookmarks… page \(page)")
            var comps = URLComponents(string: "\(usersURL)/\(id)/bookmarks")!
            var items: [URLQueryItem] = [
                // NOT 100: the bookmarks endpoint silently truncates and drops
                // meta.next_token at max_results=100 (one ~100-entry page, no
                // pagination). 50 is the highest batch size that pages reliably.
                .init(name: "max_results", value: "50"),
                .init(name: "tweet.fields", value: "created_at,author_id"),
                .init(name: "expansions", value: "author_id"),
                .init(name: "user.fields", value: "name,username"),
            ]
            if let pageToken { items.append(.init(name: "pagination_token", value: pageToken)) }
            comps.queryItems = items
            var req = URLRequest(url: comps.url!)
            req.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization")
            let (data, resp) = try await URLSession.shared.data(for: req)
            let status = (resp as? HTTPURLResponse)?.statusCode ?? -1
            guard status == 200 else {
                let msg = Self.apiMessage(data) ?? "bookmarks fetch failed (HTTP \(status))"
                if out.isEmpty { throw XError(msg) }
                out += "…stopped early (\(msg)) — the bookmarks above were fetched before the error.\n"
                return out
            }
            out += Self.feedMarkdown(data, filterNoise: false)
            let obj = try? JSONSerialization.jsonObject(with: data) as? [String: Any]
            pageToken = (obj?["meta"] as? [String: Any])?["next_token"] as? String
            if pageToken == nil { break }
        }
        return out
    }

    /// Render the timeline JSON into markdown entries. Empty string if the
    /// feed has no posts (the caller reports that as a non-error).
    /// filterNoise applies the news feed's skip rules (image-only posts,
    /// one-liners) — off for bookmarks, which are kept verbatim.
    private static func feedMarkdown(_ data: Data, filterNoise: Bool = true) -> String {
        guard let obj = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let tweets = obj["data"] as? [[String: Any]], !tweets.isEmpty
        else { return "" }

        // author_id -> (display name, @handle) from the expanded includes
        var users: [String: (name: String, handle: String)] = [:]
        if let inc = obj["includes"] as? [String: Any],
           let us = inc["users"] as? [[String: Any]] {
            for u in us {
                guard let uid = u["id"] as? String else { continue }
                users[uid] = ((u["name"] as? String) ?? "Unknown",
                              (u["username"] as? String) ?? "unknown")
            }
        }

        let isoFrac = ISO8601DateFormatter()
        isoFrac.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
        let iso = ISO8601DateFormatter()
        let dayFmt = DateFormatter(); dayFmt.dateFormat = "yyyy-MM-dd"
        let hmFmt = DateFormatter(); hmFmt.dateFormat = "HH:mm"
        let stampFmt = DateFormatter(); stampFmt.dateFormat = "yyyy-MM-dd HH:mm"

        var out = ""
        for t in tweets {
            let text = unescapeEntities((t["text"] as? String) ?? "")
            // Skip the noise: image/link-only posts and one-liners
            // (kern_feed_skip_post, the headless-tested C filter).
            if filterNoise, text.withCString({ kern_feed_skip_post($0) }) != 0 { continue }
            let tid = (t["id"] as? String) ?? ""
            let author = users[(t["author_id"] as? String) ?? ""] ?? ("Unknown", "unknown")
            var day = "", hm = "", stamp = ""
            if let created = t["created_at"] as? String,
               let d = isoFrac.date(from: created) ?? iso.date(from: created) {
                day = dayFmt.string(from: d)
                hm = hmFmt.string(from: d)
                stamp = stampFmt.string(from: d)
            }

            out += day.isEmpty ? "## \(author.name)\n\n"
                               : "## \(author.name) — \(day) at \(hm)\n\n"
            // The post text renders as a markdown blockquote — every line gets
            // a "> " prefix (kern_feed_quote_text, the headless-tested C helper).
            var quoted = [CChar](repeating: 0, count: text.utf8.count * 3 + 16)
            let n = quoted.count
            text.withCString { src in
                quoted.withUnsafeMutableBufferPointer { buf in
                    kern_feed_quote_text(src, buf.baseAddress, Int32(n))
                }
            }
            out += String(cString: quoted) + "\n\n"
            out += "\(author.name) (@\(author.handle)) — \(stamp)\n"
            out += "https://x.com/\(author.handle)/status/\(tid)\n\n"
        }
        return out
    }

    /// The v2 API HTML-escapes &, <, > in tweet text; undo that for the note.
    private static func unescapeEntities(_ s: String) -> String {
        s.replacingOccurrences(of: "&lt;", with: "<")
         .replacingOccurrences(of: "&gt;", with: ">")
         .replacingOccurrences(of: "&amp;", with: "&")
    }

    private func store(_ t: XTokens) {
        Keychain.saveTokens(t)
        tokens = t
    }

    // MARK: helpers

    private static func formEncode(_ form: [String: String]) -> String {
        var allowed = CharacterSet.alphanumerics
        allowed.insert(charactersIn: "-._~")
        return form.map {
            "\($0)=\($1.addingPercentEncoding(withAllowedCharacters: allowed) ?? "")"
        }.joined(separator: "&")
    }

    private static func apiMessage(_ data: Data) -> String? {
        guard let obj = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else { return nil }
        if let detail = obj["detail"] as? String { return detail }                 // v2 problem+json
        if let errs = obj["errors"] as? [[String: Any]],
           let msg = errs.first?["message"] as? String { return msg }
        if let desc = obj["error_description"] as? String { return desc }           // OAuth errors
        if let err = obj["error"] as? String { return err }
        return nil
    }

    private static func randomURLSafe(_ count: Int) -> String {
        var bytes = [UInt8](repeating: 0, count: count)
        _ = SecRandomCopyBytes(kSecRandomDefault, count, &bytes)
        return base64URL(Data(bytes))
    }

    private static func codeChallenge(for verifier: String) -> String {
        base64URL(Data(SHA256.hash(data: Data(verifier.utf8))))
    }

    private static func base64URL(_ data: Data) -> String {
        data.base64EncodedString()
            .replacingOccurrences(of: "+", with: "-")
            .replacingOccurrences(of: "/", with: "_")
            .replacingOccurrences(of: "=", with: "")
    }
}

struct XTokens: Codable {
    var accessToken: String
    var refreshToken: String
    var expiresAt: Date
    var username: String?
    var name: String?        // display name ("Alexis Rondeau")
    var avatarURL: String?   // profile_image_url (upscaled to _400x400)
    var userID: String?      // numeric id, keys the home-timeline endpoint
}

struct XError: LocalizedError {
    let msg: String
    init(_ msg: String) { self.msg = msg }
    var errorDescription: String? { msg }
}

// MARK: - Loopback OAuth listener

/// A throwaway HTTP listener on 127.0.0.1 that catches the single OAuth redirect
/// (`GET /callback?code=...`), replies with a friendly page, and hands back the
/// query items. Runs entirely off the main thread.
nonisolated final class LoopbackServer {
    private let port: UInt16
    private var listener: NWListener?
    private let lock = NSLock()
    private var finished = false

    init(port: UInt16) { self.port = port }

    func waitForCallback() async throws -> [URLQueryItem] {
        try await withCheckedThrowingContinuation { (cont: CheckedContinuation<[URLQueryItem], Error>) in
            let resumeOnce: (Result<[URLQueryItem], Error>) -> Void = { result in
                self.lock.lock()
                let first = !self.finished
                self.finished = true
                self.lock.unlock()
                if first { cont.resume(with: result) }
            }

            let listener: NWListener
            do {
                let params = NWParameters.tcp
                params.allowLocalEndpointReuse = true
                // Bind the socket to 127.0.0.1 specifically so it is never
                // reachable from the network — only this machine's browser.
                params.requiredLocalEndpoint = .hostPort(host: "127.0.0.1",
                                                         port: NWEndpoint.Port(rawValue: port)!)
                listener = try NWListener(using: params)
            } catch {
                resumeOnce(.failure(XError("Couldn't open local port \(port): \(error.localizedDescription)")))
                return
            }
            self.listener = listener

            listener.stateUpdateHandler = { state in
                switch state {
                case .failed(let err):
                    resumeOnce(.failure(XError("Local listener failed: \(err.localizedDescription)")))
                case .waiting(let err):
                    resumeOnce(.failure(XError("Local listener can't bind port \(self.port): \(err.localizedDescription)")))
                default:
                    break
                }
            }
            listener.newConnectionHandler = { conn in
                conn.start(queue: .global())
                conn.receive(minimumIncompleteLength: 1, maximumLength: 65536) { data, _, _, _ in
                    guard let data, let request = String(data: data, encoding: .utf8) else { return }
                    // First line: "GET /callback?code=...&state=... HTTP/1.1"
                    let target = request.split(separator: " ").dropFirst().first.map(String.init) ?? ""
                    let items = URLComponents(string: "http://127.0.0.1\(target)")?.queryItems ?? []
                    // Ignore noise like /favicon.ico — only the callback carries params.
                    guard !items.isEmpty else { conn.cancel(); return }

                    let body = """
                    <!doctype html><html><head><meta charset="utf-8"><title>Kern</title></head>\
                    <body style="font-family:-apple-system,system-ui;text-align:center;padding-top:90px;color:#111">\
                    <h2>Kern is connected ✓</h2><p>You can close this tab and return to Kern.</p></body></html>
                    """
                    let resp = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n" +
                               "Content-Length: \(body.utf8.count)\r\nConnection: close\r\n\r\n\(body)"
                    conn.send(content: resp.data(using: .utf8), completion: .contentProcessed { _ in conn.cancel() })
                    resumeOnce(.success(items))
                }
            }
            listener.start(queue: .global())
        }
    }

    func stop() { listener?.cancel(); listener = nil }
}

// MARK: - Keychain

enum Keychain {
    private static let service = "me.alexisrondeau.kern.x"
    private static let account = "tokens"

    static func saveTokens(_ t: XTokens) {
        guard let data = try? JSONEncoder().encode(t) else { return }
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: account,
        ]
        SecItemDelete(query as CFDictionary)
        var add = query
        add[kSecValueData as String] = data
        SecItemAdd(add as CFDictionary, nil)
    }

    static func loadTokens() -> XTokens? {
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: account,
            kSecReturnData as String: true,
            kSecMatchLimit as String: kSecMatchLimitOne,
        ]
        var out: AnyObject?
        guard SecItemCopyMatching(query as CFDictionary, &out) == errSecSuccess,
              let data = out as? Data else { return nil }
        return try? JSONDecoder().decode(XTokens.self, from: data)
    }

    static func deleteTokens() {
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: account,
        ]
        SecItemDelete(query as CFDictionary)
    }
}

// MARK: - C bridge

/// 1 if an X account is linked, else 0. Synchronous; safe from the editor's
/// keystroke handler before kicking off a publish.
@_cdecl("kern_x_is_connected")
public func kern_x_is_connected() -> Int32 {
    XAuth.shared.isConnected ? 1 : 0
}

/// Fire-and-forget publish. Returns immediately; the result is reported back to
/// the editor's status bar via kern_x_set_status() once the network call lands.
@_cdecl("kern_x_publish")
public func kern_x_publish(_ ctext: UnsafePointer<CChar>?) {
    guard let ctext else { return }
    let text = String(cString: ctext)
    Task.detached {
        do {
            let url = try await XAuth.shared.post(text: text)
            // The confirmation overlay applies this on the main-thread tick:
            // closes, reports success, copies the URL to the clipboard.
            url.withCString { kern_x_publish_done(1, $0) }
            notifyX("Posted to X \u{2713}", "Your note is live on X.")
        } catch {
            let msg = "X: \(error.localizedDescription)"
            msg.withCString { kern_x_publish_done(0, $0) }
            notifyX("X publish failed", error.localizedDescription)
        }
    }
}

/// Caches a C string so the pointer stays valid across the per-frame calls the
/// preview overlay makes (rebuilt only when the string changes).
private final class CStringCache {
    private let lock = NSLock()
    private var ptr: UnsafeMutablePointer<CChar>?
    private var last: String?
    func get(_ s: String) -> UnsafePointer<CChar>? {
        lock.lock(); defer { lock.unlock() }
        if last != s || ptr == nil { free(ptr); ptr = strdup(s); last = s }
        return UnsafePointer(ptr)
    }
}
private let xNameCache = CStringCache()
private let xHandleCache = CStringCache()

/// The connected account's display name ("" if unknown). Read every frame by
/// the tweet-preview overlay.
@_cdecl("kern_x_display_name")
public func kern_x_display_name() -> UnsafePointer<CChar>? {
    xNameCache.get(XAuth.shared.displayName ?? "")
}

/// The connected account's @handle, without the leading '@' ("" if unknown).
@_cdecl("kern_x_handle")
public func kern_x_handle() -> UnsafePointer<CChar>? {
    xHandleCache.get(XAuth.shared.username ?? "")
}

/// Fire-and-forget home-timeline fetch (C-x n). Returns immediately; the feed
/// (formatted as markdown entries) or an error lands in kern_x_feed_done(),
/// which the editor applies on its next main-thread tick.
@_cdecl("kern_x_fetch_feed")
public func kern_x_fetch_feed() {
    Task.detached {
        do {
            let md = try await XAuth.shared.homeTimeline()
            if md.isEmpty {
                "No substantial posts in your feed right now".withCString { kern_x_feed_done(0, $0) }
            } else {
                md.withCString { kern_x_feed_done(1, $0) }
            }
        } catch {
            let msg = "X: \(error.localizedDescription)"
            msg.withCString { kern_x_feed_done(0, $0) }
        }
    }
}

/// Fire-and-forget bookmarks fetch (C-x m). Returns immediately; every page of
/// bookmarks (formatted as markdown entries) or an error lands in
/// kern_x_bookmarks_done(), applied on the editor's next main-thread tick.
@_cdecl("kern_x_fetch_bookmarks")
public func kern_x_fetch_bookmarks() {
    Task.detached {
        do {
            let md = try await XAuth.shared.bookmarks()
            if md.isEmpty {
                "No bookmarks found".withCString { kern_x_bookmarks_done(0, $0) }
            } else {
                md.withCString { kern_x_bookmarks_done(1, $0) }
            }
        } catch {
            let msg = "X: \(error.localizedDescription)"
            msg.withCString { kern_x_bookmarks_done(0, $0) }
        }
    }
}

/// Tightly-packed RGBA pixels of the profile photo (top row first), or NULL if
/// it hasn't downloaded yet — the overlay then draws an initials disc.
@_cdecl("kern_x_avatar_rgba")
public func kern_x_avatar_rgba(_ w: UnsafeMutablePointer<Int32>?,
                               _ h: UnsafeMutablePointer<Int32>?) -> UnsafePointer<UInt8>? {
    guard let (p, aw, ah) = XAuth.shared.avatar() else {
        w?.pointee = 0; h?.pointee = 0; return nil
    }
    w?.pointee = Int32(aw); h?.pointee = Int32(ah)
    return p
}
