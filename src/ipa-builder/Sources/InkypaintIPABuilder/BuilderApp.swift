// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import AppKit
import SwiftUI

@main
struct BuilderApp: App {
    @NSApplicationDelegateAdaptor(BuilderDelegate.self) private var delegate
    @StateObject private var model = BuilderModel()
    var body: some Scene {
        Window("Inkypaint IPA Builder", id: "builder") {
            BuilderView(model: model)
                .onAppear { delegate.model = model }
                .frame(minWidth: 760, minHeight: 590)
        }
        .defaultSize(width: 900, height: 700)
        .commands { CommandGroup(replacing: .newItem) {} }
    }
}

@MainActor
final class BuilderDelegate: NSObject, NSApplicationDelegate {
    weak var model: BuilderModel?
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        guard let model, model.busy else { return .terminateNow }
        model.onStopped = { NSApp.reply(toApplicationShouldTerminate: true) }
        model.stop()
        return .terminateLater
    }
}

struct BuilderView: View {
    @ObservedObject var model: BuilderModel
    private let ink = Color(red: 0.10, green: 0.055, blue: 0.20)
    var body: some View {
        VStack(alignment: .leading, spacing: 18) {
            HStack(spacing: 14) {
                if let url = Bundle.main.url(forResource: "splat-burst", withExtension: "png"),
                   let image = NSImage(contentsOf: url) {
                    Image(nsImage: image).renderingMode(.template).resizable().scaledToFit()
                        .foregroundStyle(Color.yellow).frame(width: 58, height: 58)
                }
                VStack(alignment: .leading, spacing: 4) {
                    Text("inkypaint").font(.system(size: 30, weight: .black, design: .rounded))
                    Text("IPA Builder").font(.title3.bold())
                }
                Spacer()
                Text("iPhone / iPad").font(.callout).foregroundStyle(.secondary)
            }
            VStack(alignment: .leading, spacing: 12) {
                pathRow("Project folder", text: $model.project, action: model.chooseProject)
                pathRow("Existing Mac game", text: $model.installation, action: model.chooseInstallation)
                HStack {
                    Button("Find installed game", action: model.useExistingInstall)
                    Spacer()
                    Stepper("Build workers: \(model.workers)", value: $model.workers, in: 1...max(8, ProcessInfo.processInfo.activeProcessorCount))
                        .fixedSize()
                }
                Text("Reuses your installed game files and compiles iOS code. Includes game assets in the IPA; signing is separate.")
                    .font(.callout).foregroundStyle(.secondary)
                Toggle("Include installed DLC (optional, untested)", isOn: $model.includeDlc)
                Text("Install and extract DLC with the desktop launcher first. Leave this off for the base game only.")
                    .font(.caption).foregroundStyle(.secondary)
            }
            .disabled(model.busy)
            HStack {
                if model.busy { ProgressView().controlSize(.small) }
                Text(model.status).font(.headline).textSelection(.enabled)
                Spacer()
                if model.busy {
                    Button("Stop", action: model.stop)
                } else {
                    Button("Check", action: { model.run(check: true) })
                    Button("Build IPA", action: { model.run(check: false) })
                        .buttonStyle(.borderedProminent)
                }
            }
            TextEditor(text: .constant(model.log))
                .font(.system(size: 11, design: .monospaced))
                .scrollContentBackground(.hidden)
                .padding(8).background(.black.opacity(0.25), in: RoundedRectangle(cornerRadius: 12))
                .accessibilityLabel("Build output")
            HStack {
                Text("The full build log is kept in the project's logs folder.")
                    .font(.caption).foregroundStyle(.secondary)
                Spacer()
                Button("Show logs", action: model.showLogs)
                Button("Show IPA", action: model.showIPA).disabled(model.builtIPA == nil)
            }
        }
        .padding(24)
        .background(LinearGradient(colors: [ink, Color(red: 0.22, green: 0.10, blue: 0.39)], startPoint: .topLeading, endPoint: .bottomTrailing))
        .preferredColorScheme(.dark)
        .tint(Color(red: 0.48, green: 0.28, blue: 1))
    }
    private func pathRow(_ label: String, text: Binding<String>, action: @escaping () -> Void) -> some View {
        VStack(alignment: .leading, spacing: 5) {
            Text(label).font(.subheadline.bold())
            HStack {
                TextField(label, text: text).textFieldStyle(.roundedBorder)
                Button("Choose…", action: action)
            }
        }
    }
}
