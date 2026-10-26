// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import AppKit
import SwiftUI

struct RootView: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        NavigationSplitView {
            List(selection: Binding<Pane?>(get: { model.pane }, set: { if let pane = $0 { model.pane = pane } })) {
                ForEach(Pane.allCases) { pane in
                    Label(pane.title, systemImage: pane.symbol)
                        .badge(badge(pane))
                        .tag(pane)
                }
            }
            .navigationSplitViewColumnWidth(min: 190, ideal: 210, max: 260)
            .safeAreaInset(edge: .bottom) { Wordmark().padding(16) }
        } detail: {
            switch model.pane {
            case .play: PlayView()
            case .online: OnlineView()
            case .settings: SettingsView()
            case .files: FilesView()
            }
        }
        .onChange(of: model.preparer.phase) { model.preparationChanged() }
        .onReceive(NotificationCenter.default.publisher(for: NSApplication.didBecomeActiveNotification)) { _ in
            model.refresh()
        }
    }

    private func badge(_ pane: Pane) -> Text? {
        if pane == .files, model.preparer.isRunning {
            return Text(model.preparer.fraction, format: .percent.precision(.fractionLength(0)))
        }
        if pane == .online, !model.online.incoming.isEmpty { return Text("\(model.online.incoming.count)") }
        return nil
    }
}

/// The app's name in the sidebar, inked.
private struct Wordmark: View {
    var body: some View {
        HStack(spacing: 8) {
            SplatMark(splat: .drip, color: Ink.purple, size: 30, angle: -18)
            Text("inkypaint")
                .font(.inkTitle(20))
            Circle().fill(Ink.lime).frame(width: 7, height: 7).offset(x: -6, y: 5)
            Spacer()
        }
        .accessibilityElement(children: .combine)
        .accessibilityLabel("inkypaint")
    }
}
