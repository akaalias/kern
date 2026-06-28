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
        .commands {
            // Window menu: open the documents folder in Finder (replaces the
            // former title-bar folder button).
            CommandGroup(after: .windowArrangement) {
                Button("Open Documents Folder in Finder") { kern_open_documents_folder() }
            }
            CommandGroup(after: .toolbar) {
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
            }
        }
    }
}

// MARK: - Settings UI

struct SettingsView: View {
    var body: some View {
        TabView {
            XSettingsView()
                .tabItem { Text("X (Twitter)") }
                .frame(width: 460)
                .padding(20)

            ShortcutsView()
                .tabItem { Text("Keyboard Shortcuts") }
                .frame(minWidth: 940, minHeight: 600)
        }
    }
}

/// Embeds the AppKit keyboard-shortcuts reference (built in Platform/macos_style.m)
/// in the Settings window. The C function returns a retained NSScrollView*; we
/// take ownership to balance the +1.
struct ShortcutsView: NSViewRepresentable {
    func makeNSView(context: Context) -> NSView {
        guard let ptr = kern_make_shortcuts_view() else { return NSView() }
        return Unmanaged<NSView>.fromOpaque(UnsafeRawPointer(ptr)).takeRetainedValue()
    }
    func updateNSView(_ nsView: NSView, context: Context) {}
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

            Text("Publish the note you're viewing straight to your X timeline with ⌘⇧T. "
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
    private let scopes = "tweet.read tweet.write users.read offline.access"

    private let authorizeURL = "https://x.com/i/oauth2/authorize"
    private let tokenURL = "https://api.x.com/2/oauth2/token"
    private let tweetsURL = "https://api.x.com/2/tweets"
    private let meURL = "https://api.x.com/2/users/me"

    private let lock = NSLock()
    private var _tokens: XTokens?

    private var tokens: XTokens? {
        get { lock.lock(); defer { lock.unlock() }; return _tokens }
        set { lock.lock(); _tokens = newValue; lock.unlock() }
    }

    var isConnected: Bool { tokens != nil }
    var username: String? { tokens?.username }

    private init() {
        _tokens = Keychain.loadTokens()
    }

    /// Re-read tokens from the Keychain (for the Settings "Refresh" button).
    func reload() {
        tokens = Keychain.loadTokens()
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
        fresh.username = try? await fetchUsername(accessToken: fresh.accessToken)
        store(fresh)
        reportXStatus("Connected to X as @\(fresh.username ?? "account") \u{2713}")
    }

    func disconnect() {
        Keychain.deleteTokens()
        tokens = nil
    }

    // MARK: posting

    /// Post `text` as a tweet, refreshing the access token first if needed.
    func post(text: String) async throws {
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
        fresh.username = tokens?.username   // token response omits it; carry it forward
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

    private func fetchUsername(accessToken: String) async throws -> String? {
        var req = URLRequest(url: URL(string: meURL)!)
        req.setValue("Bearer \(accessToken)", forHTTPHeaderField: "Authorization")
        let (data, _) = try await URLSession.shared.data(for: req)
        let obj = try? JSONSerialization.jsonObject(with: data) as? [String: Any]
        return (obj?["data"] as? [String: Any])?["username"] as? String
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
            try await XAuth.shared.post(text: text)
            reportXStatus("Posted to X \u{2713}")
            notifyX("Posted to X \u{2713}", "Your note is live on X.")
        } catch {
            reportXStatus("X: \(error.localizedDescription)")
            notifyX("X publish failed", error.localizedDescription)
        }
    }
}
