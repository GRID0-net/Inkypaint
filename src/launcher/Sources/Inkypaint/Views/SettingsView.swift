// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import SwiftUI

/// The game's settings that matter for playing, written into its .ini as they change. The rest are in the file itself
/// (Show Settings File) and, later, in the game's own menu.
struct SettingsView: View {
    @Environment(AppModel.self) private var model
    var inputPage = ""
    private var shownGroups: [SettingGroup] {
        SettingsCatalog.groups.filter { group in
            if inputPage == "keyboard" { return ["Keyboard and mouse", "Keys in menus"].contains(group.title) }
            if inputPage == "controller" { return group.title == "Controller input" }
            return !["Keyboard and mouse", "Keys in menus", "Controller input"].contains(group.title)
        }
    }
    @State private var confirmReset = false

    var body: some View {
        let settings = model.settings
        Form {
            if !inputPage.isEmpty { Button("Back to Settings") { model.pane = .settings } }
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

            ForEach(shownGroups) { group in
                Section {
                    if group.title == "Graphics" {
                        HStack {
                            ForEach(["Performance", "Balanced", "Quality"], id: \.self) { preset in
                                Button(preset) { settings.applyGraphicsPreset(preset) }
                            }
                        }
                        Text("Settings marked next launch need a restart. Change individual settings below.")
                            .font(.caption).foregroundStyle(.secondary)
                    }
                    if group.title == "Controls" {
                        Button("Change keyboard input") { model.pane = .keyboardInput }
                        Button("Change controller input") { model.pane = .controllerInput }
                    }
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
        .navigationTitle(inputPage == "keyboard" ? "Keyboard input" : inputPage == "controller" ? "Controller input" : "Settings")
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
        .confirmationDialog("Reset all game settings to their defaults?", isPresented: $confirmReset) {
            Button("Reset", role: .destructive) { settings.resetAll() }
        }
        .onAppear { settings.reload() }
    }

    @ViewBuilder
    private func row(_ def: SettingDef, _ settings: GameSettings) -> some View {
        switch def.kind {
        case .controllerMap:
            ForEach(SettingsCatalog.controllerControls, id: \.key) { control in
                Picker(control.title, selection: Binding(get: { settings.controllerValue(control.key) }, set: { settings.setControllerMapping(control.key, $0) })) {
                    ForEach(SettingsCatalog.controllerButtons, id: \.self) { Text($0).tag($0) }
                }
            }
        case .toggle:
            Toggle(isOn: Binding(get: { settings.toggle(def) }, set: { settings.set(def, $0 ? "1" : "0") })) {
                title(def)
            }
            .tint(Ink.purple)
        case .networkAdapter:
            choice(def, settings, NetworkAdapter.choices())
        case .choice(let options):
            choice(def, settings, options)
        case .key(let fallback):
            LabeledContent {
                KeyCaptureField(current: settings.value(def), fallback: fallback) { settings.set(def, $0) }
            } label: {
                title(def)
            }
        }
    }

    @ViewBuilder
    private func choice(_ def: SettingDef, _ settings: GameSettings,
                        _ options: [(value: String, label: String)]) -> some View {
        Picker(selection: Binding(get: { settings.value(def) }, set: { settings.set(def, $0) })) {
            ForEach(options.indices, id: \.self) { i in Text(options[i].label).tag(options[i].value) }
            // a value set in the file by hand (or an adapter not connected now) stays selectable
            if !options.contains(where: { $0.value == settings.value(def) }) {
                Text(settings.value(def)).tag(settings.value(def))
            }
        } label: {
            title(def)
        }
        .disabled(def.key == "fps_limit" && !settings.smoothMotionEnabled)
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
