//
//  MicroEditApp.swift
//  MicroEdit
//
//  Created by Alexis Rondeau on 18.06.26.
//

import SwiftUI

class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationDidFinishLaunching(_ notification: Notification) {
        // Hand off to the C editor once the app is a proper foreground app.
        // editor_main runs SDL's own window + event loop (blocking); for this
        // test the SwiftUI window is just the launcher shell.
        DispatchQueue.main.async {
            editor_main(0, nil)
        }
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        return true
    }
}

@main
struct MicroEditApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) var appDelegate

    var body: some Scene {
        // No WindowGroup: the editor runs in its own SDL window. The Settings
        // scene satisfies the App protocol without opening a window at launch.
        Settings {
            EmptyView()
        }
    }
}
