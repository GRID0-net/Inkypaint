// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import AppKit
import SwiftUI

@main
struct InkypaintApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var delegate
    @State private var model = AppModel()

    var body: some Scene {
        Window("inkypaint", id: "main") {
            RootView()
                .environment(model)
                .frame(minWidth: 860, minHeight: 560)
        }
        .defaultSize(width: 1040, height: 660)
        .windowToolbarStyle(.unified)
        .commands {
            CommandGroup(replacing: .newItem) {}
            CommandMenu("Game") {
                Button("Play") { model.play() }
                    .keyboardShortcut("r")
                    .disabled(!model.isPrepared || model.isPlaying)
                Button("Quit Game") { model.stop() }
                    .keyboardShortcut(".")
                    .disabled(!model.isPlaying)
                Divider()
                Button("Show Game Folder") { model.paths.gameDir.revealInFinder() }
                    .disabled(!model.isPrepared)
                Button("Show Launcher Logs") { GamePaths.logs.revealInFinder() }
            }
            CommandGroup(after: .sidebar) {
                ForEach(Array(Pane.allCases.enumerated()), id: \.element) { i, pane in
                    Button(pane.title) { model.pane = pane }
                        .keyboardShortcut(KeyEquivalent(Character(String(i + 1))), modifiers: .command)
                }
            }
        }
    }
}

/// The app quits with its window. A game started from it is a process of its own and keeps running.
final class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
}
