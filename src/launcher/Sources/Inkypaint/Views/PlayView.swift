// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import SwiftUI

struct PlayView: View {
    @Environment(AppModel.self) private var model
    @State private var bursts = 0
    @Namespace private var glass

    var body: some View {
        ZStack(alignment: .bottomLeading) {
            InkBackdrop()
                .backgroundExtensionEffect()

            VStack(alignment: .leading, spacing: 18) {
                Spacer(minLength: 0)
                header
                actions
                if model.isPrepared { summary }
                if let message = model.lastExit {
                    Label(message, systemImage: "exclamationmark.triangle.fill")
                        .font(.callout)
                        .padding(.horizontal, 14).padding(.vertical, 10)
                        .glassEffect(.regular.tint(.orange.opacity(0.3)), in: .capsule)
                }
            }
            .padding(36)
            .frame(maxWidth: 620, alignment: .leading)
        }
        .navigationTitle("")
        .toolbar {
            ToolbarItem(placement: .primaryAction) {
                Button {
                    model.paths.gameDir.revealInFinder()
                } label: {
                    Label("Show Game Folder", systemImage: "folder")
                }
                .disabled(!model.isPrepared)
            }
        }
    }

    private var header: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack(spacing: 8) {
                Text("VERSION \(Game.version)")
                    .font(.caption.weight(.heavy).monospaced())
                Text("·")
                Text(statusLine)
                    .font(.caption.weight(.semibold))
            }
            .foregroundStyle(.white.opacity(0.9))
            .padding(.horizontal, 12).padding(.vertical, 6)
            .glassEffect(.regular, in: .capsule)

            Text(Game.name)
                .font(.inkTitle(68))
                .foregroundStyle(.white)
                .rotationEffect(.degrees(-3), anchor: .leading)
                .shadow(color: Ink.purple.opacity(0.8), radius: 0, x: 4, y: 4)
                .shadow(color: .black.opacity(0.35), radius: 18, y: 8)
                .accessibilityAddTraits(.isHeader)
        }
    }

    private var statusLine: String {
        if model.isPlaying, let started = model.playStarted {
            return "Playing since \(started.formatted(date: .omitted, time: .shortened))"
        }
        if !model.isPrepared { return "Not set up yet" }
        if let last = model.lastPlayed { return "Last played \(last.formatted(.relative(presentation: .named)))" }
        return "Ready to play"
    }

    @ViewBuilder private var actions: some View {
        GlassEffectContainer(spacing: 12) {
            HStack(spacing: 12) {
                if !model.isPrepared {
                    Button {
                        model.pane = .files
                    } label: {
                        Label("Set Up Splatoon 3", systemImage: "arrow.down.circle.fill")
                            .font(.title3.weight(.bold))
                            .padding(.horizontal, 10).padding(.vertical, 4)
                    }
                    .buttonStyle(.glassProminent)
                    .tint(Ink.purple)
                    .controlSize(.extraLarge)
                } else if model.isPlaying {
                    Label("Playing", systemImage: "gamecontroller.fill")
                        .font(.title3.weight(.bold))
                        .padding(.horizontal, 22).padding(.vertical, 12)
                        .glassEffect(.regular.tint(Ink.lime.opacity(0.35)), in: .capsule)
                        .glassEffectID("play", in: glass)
                    Button(role: .destructive) {
                        model.stop()
                    } label: {
                        Label("Quit Game", systemImage: "stop.fill")
                            .padding(.vertical, 4)
                    }
                    .buttonStyle(.glass)
                    .controlSize(.extraLarge)
                    .glassEffectID("stop", in: glass)
                } else {
                    Button {
                        bursts += 1
                        model.play()
                    } label: {
                        Label("Play", systemImage: "play.fill")
                            .font(.title2.weight(.black))
                            .foregroundStyle(.black)
                            .padding(.horizontal, 26).padding(.vertical, 6)
                    }
                    .buttonStyle(.glassProminent)
                    .tint(Ink.lime)
                    .controlSize(.extraLarge)
                    .keyboardShortcut(.defaultAction)
                    .glassEffectID("play", in: glass)
                    .background(alignment: .center) { InkBurst(trigger: bursts) }

                    Button {
                        model.pane = .settings
                    } label: {
                        Image(systemName: "slider.horizontal.3")
                            .font(.title3.weight(.semibold))
                            .padding(.vertical, 6)
                    }
                    .buttonStyle(.glass)
                    .controlSize(.extraLarge)
                    .help("Settings")
                    .glassEffectID("settings", in: glass)
                }
            }
        }
        .animation(.spring(response: 0.4, dampingFraction: 0.8), value: model.isPlaying)
    }

    private var summary: some View {
        GlassEffectContainer(spacing: 10) {
            HStack(spacing: 10) {
                Tile(title: "Resolution", value: model.settings.label("resolution"), symbol: "viewfinder")
                Tile(title: "Frame rate", value: model.settings.label("fps_limit"), symbol: "speedometer")
                Tile(title: "Shaders", value: model.settings.toggle(
                    SettingsCatalog.all.first { $0.key == "async_pipelines" }!) ? "In the background" : "Waited for",
                     symbol: "sparkles")
            }
        }
    }
}

private struct Tile: View {
    var title: String
    var value: String
    var symbol: String

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Label(title, systemImage: symbol)
                .font(.caption.weight(.semibold))
                .foregroundStyle(.secondary)
            Text(value)
                .font(.callout.weight(.bold))
                .lineLimit(1)
                .minimumScaleFactor(0.8)
        }
        .padding(.horizontal, 14).padding(.vertical, 10)
        .frame(minWidth: 130, alignment: .leading)
        .glassEffect(.regular, in: .rect(cornerRadius: 16))
    }
}
