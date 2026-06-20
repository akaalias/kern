//
//  KernApp.swift
//  Kern
//
//  Created by Alexis Rondeau on 18.06.26.
//

import SwiftUI

class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationDidFinishLaunching(_ notification: Notification) {
        // Point the editor's file I/O at the app's sandbox-container Documents
        // folder (~/Library/Containers/<id>/Data/Documents), creating it if needed.
        let fm = FileManager.default
        if let docs = fm.urls(for: .documentDirectory, in: .userDomainMask).first {
            try? fm.createDirectory(at: docs, withIntermediateDirectories: true)
            docs.path.withCString { editor_set_documents_dir($0) }
        }

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
        // scene satisfies the App protocol without opening a window at launch.
        Settings {
            EmptyView()
        }
    }
}
