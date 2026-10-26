// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import SwiftUI

/// The game's settings that matter for playing, written into its .ini as they change. The rest are in the file itself
/// (Show Settings File) and, later, in the game's own menu.
struct SettingsView: View {
    @Environment(AppModel.self) private var model
    @State private var confirmReset = false

    var body: some View {
        let settings = model.settings
        Form {
            if model.isPlaying {
                Section {
                    Label("The game is running. Settings marked “next launch” apply when you start it again.",
                          systemImage: "info.circle")
                        .foregroundStyle(.secondary)
                }
            } else if !settings.exists {
                Section {
                    Label("The game writes its full settings file the first time it starts. These are saved for it.",
                          systemImage: "info.circle")
                        .foregroundStyle(.secondary)
                }
            }

            ForEach(SettingsCatalog.groups) { group in
                Section {
                    ForEach(group.settings) { def in row(def, settings) }
                } header: {
                    HStack(spacing: 10) {
                        Image(systemName: group.symbol)
                            .foregroundStyle(Ink.purple)
                            .frame(width: 18)
                        Text(group.title)
                    }
                    .font(.headline)
                }
            }

            Section("Launcher") {
                Toggle(isOn: Binding(get: { model.hideWhilePlaying }, set: { model.setHideWhilePlaying($0) })) {
                    Text("Hide the launcher while playing")
                    Text("It comes back when the game quits.")
                }
            }

            Section {
                HStack {
                    Button("Show Settings File") { settings.url.revealInFinder() }
                        .disabled(!settings.exists)
                    Spacer()
                    Button("Reset to Defaults", role: .destructive) { confirmReset = true }
                }
                if let error = settings.error {
                    Label(error, systemImage: "exclamationmark.triangle.fill").foregroundStyle(.orange)
                }
            }
        }
        .formStyle(.grouped)
        .navigationTitle("Settings")
        .disabled(!model.isPrepared && !settings.exists)
        .overlay {
            if !model.isPrepared && !settings.exists {
                ContentUnavailableView {
                    Label("Set Up the Game First", systemImage: "slider.horizontal.3")
                } description: {
                    Text("Settings belong to the prepared game.")
                } actions: {
                    Button("Go to Game Files") { model.pane = .files }
                        .buttonStyle(.glassProminent)
                        .tint(Ink.purple)
                }
                .glassEffect(.regular, in: .rect(cornerRadius: 28))
                .padding(40)
            }
        }
        .confirmationDialog("Reset the game's settings shown here to their defaults?", isPresented: $confirmReset) {
            Button("Reset", role: .destructive) { settings.resetAll() }
        }
        .onAppear { settings.reload() }
    }

    @ViewBuilder
    private func row(_ def: SettingDef, _ settings: GameSettings) -> some View {
        switch def.kind {
        case .toggle:
            Toggle(isOn: Binding(get: { settings.toggle(def) }, set: { settings.set(def, $0 ? "1" : "0") })) {
                title(def)
            }
            .tint(Ink.purple)
        case .choice(let options):
            Picker(selection: Binding(get: { settings.value(def) }, set: { settings.set(def, $0) })) {
                ForEach(options.indices, id: \.self) { i in Text(options[i].label).tag(options[i].value) }
                // a value set in the file by hand stays selectable
                if !options.contains(where: { $0.value == settings.value(def) }) {
                    Text(settings.value(def)).tag(settings.value(def))
                }
            } label: {
                title(def)
            }
        }
    }

    @ViewBuilder
    private func title(_ def: SettingDef) -> some View {
        HStack(spacing: 6) {
            Text(def.title)
            if def.nextLaunch {
                Text("next launch")
                    .font(.caption2.weight(.semibold))
                    .foregroundStyle(.secondary)
                    .padding(.horizontal, 6).padding(.vertical, 1)
                    .background(.quaternary, in: .capsule)
            }
        }
        if !def.detail.isEmpty { Text(def.detail) }
    }
}
