// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import AppKit
import SwiftUI
import UniformTypeIdentifiers

/// The game's mods: adding one from a folder or an archive, turning each on or off, removing it. Files (romfs/) apply
/// the next time the game starts; code (exefs/) takes a recompile, which the page offers when it is due.
struct ModsView: View {
    @Environment(AppModel.self) private var model
    @State private var choosing = false
    @State private var removing: String?
    @State private var dropping = false

    var body: some View {
        let mods = model.mods
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                hero
                if let error = mods.error {
                    ModsBanner(text: error, symbol: "exclamationmark.triangle.fill", tint: .orange) { mods.error = nil }
                } else if let notice = mods.notice {
                    ModsBanner(text: notice, symbol: "checkmark.circle.fill", tint: Ink.lime) { mods.notice = nil }
                }
                if mods.needsRecompile { recompileCard }
                addCard
                listCard
            }
            .padding(28)
            .frame(maxWidth: 760)
            .frame(maxWidth: .infinity)
        }
        .navigationTitle("Mods")
        .onAppear { model.refreshMods() }
        .fileImporter(isPresented: $choosing, allowedContentTypes: [.folder, .zip, .archive], allowsMultipleSelection: true) { result in
            guard case .success(let urls) = result else { return }
            add(urls)
        }
        .confirmationDialog("Remove \(removing ?? "")?", isPresented: Binding(get: { removing != nil }, set: { if !$0 { removing = nil } })) {
            Button("Remove", role: .destructive) {
                if let name = removing { mods.remove(name, model.paths, model.settings) }
                removing = nil
            }
        } message: {
            Text("Its files are deleted from the mods folder.")
        }
    }

    private func add(_ urls: [URL]) {
        Task {
            for url in urls {
                let scoped = url.startAccessingSecurityScopedResource()
                await model.mods.add(url, model.paths, model.settings)
                if scoped { url.stopAccessingSecurityScopedResource() }
            }
        }
    }

    private var hero: some View {
        HStack(alignment: .center, spacing: 16) {
            SplatMark(splat: .burst, color: Ink.lime, size: 74, angle: -12)
                .overlay { Image(systemName: "puzzlepiece.extension.fill").font(.title2.weight(.semibold)).foregroundStyle(.black) }
            VStack(alignment: .leading, spacing: 4) {
                Text("Mods").font(.inkTitle(30))
                Text("Files that change the game's models, textures, text or sounds work right away: no recompile. "
                     + "Mods that change the game's code need one.")
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
        .padding(.bottom, 4)
    }

    private var recompileCard: some View {
        GlassCard(tint: Ink.lime) {
            HStack(spacing: 14) {
                Image(systemName: "hammer.fill").font(.title2).foregroundStyle(Ink.lime).frame(width: 32)
                VStack(alignment: .leading, spacing: 3) {
                    Text("Recompile for the code mods").font(.headline)
                    Text(model.preparer.isRunning ? "Recompiling…"
                         : "The mods that change the game's code aren't compiled in yet. Until then the game runs without them (their files still apply).")
                        .font(.callout).foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                }
                Spacer(minLength: 12)
                Button("Recompile Now") { model.recompile() }
                    .buttonStyle(.glassProminent)
                    .tint(Ink.lime)
                    .foregroundStyle(.black)
                    .disabled(model.preparer.isRunning || model.isPlaying || model.setupStep != .idle)
            }
        }
    }

    private var addCard: some View {
        GlassCard(tint: dropping ? Ink.lime : nil) {
            VStack(alignment: .leading, spacing: 12) {
                HStack(spacing: 10) {
                    Button("Add Mod…", systemImage: "plus") { choosing = true }
                        .buttonStyle(.glassProminent)
                        .disabled(model.mods.busy || model.isPlaying)
                    Spacer()
                    if model.mods.busy { ProgressView().controlSize(.small) }
                    Button("Show in Finder") {
                        let folder = model.mods.folder(model.paths)
                        try? FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
                        folder.revealInFinder()
                    }
                    .buttonStyle(.glass)
                }
                Text(model.mods.busy ? "Adding the mod…"
                     : "A folder or an archive (.zip, .7z, .rar), or drop one here. inkypaint finds its romfs and exefs folders "
                     + "inside, including atmosphere/contents/0100C2500FC20000. Add the whole SD bundle to keep companion folders such as flexlion3. Mods load in name order: where two "
                     + "change the same file, the later one wins.")
                    .font(.caption).foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
                if model.isPlaying {
                    Text("Changes apply the next time the game starts.").font(.caption).foregroundStyle(.secondary)
                }
            }
        }
        .onDrop(of: [.fileURL], isTargeted: $dropping) { providers in
            guard !model.mods.busy, !model.isPlaying else { return false }
            for provider in providers {
                _ = provider.loadObject(ofClass: URL.self) { url, _ in
                    if let url { Task { @MainActor in add([url]) } }
                }
            }
            return true
        }
    }

    private var listCard: some View {
        GlassCard {
            VStack(alignment: .leading, spacing: 12) {
                Text("Installed").font(.headline)
                if model.mods.mods.isEmpty {
                    VStack(spacing: 4) {
                        Text("No mods yet").font(.headline)
                        Text("Add a folder or an archive above.").foregroundStyle(.secondary)
                    }
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 10)
                }
                ForEach(model.mods.mods) { mod in
                    HStack(spacing: 14) {
                        VStack(alignment: .leading, spacing: 2) {
                            Text(mod.name).font(.headline).lineLimit(1).truncationMode(.middle)
                            Text(kind(mod)).font(.caption).foregroundStyle(.secondary)
                        }
                        Spacer(minLength: 12)
                        Toggle("On", isOn: Binding(get: { mod.on }, set: { model.mods.setOn(mod.name, $0, model.paths, model.settings) }))
                            .labelsHidden()
                            .toggleStyle(.switch)
                            .disabled(model.isPlaying)
                        Button("Remove", role: .destructive) { removing = mod.name }
                            .buttonStyle(.glass)
                            .disabled(model.isPlaying || model.mods.busy)
                    }
                }
            }
        }
    }

    private func kind(_ mod: ModsModel.Mod) -> String {
        if let issue = mod.issue { return issue }
        if mod.sdmc { return mod.code ? "Code and SD-card data: the code takes a recompile" : "SD-card data: no recompile needed" }
        if mod.code && mod.romfs { return "Files and code: the code takes a recompile" }
        if mod.code { return "Code: takes a recompile" }
        if mod.romfs { return "Files: no recompile needed" }
        return "Nothing inkypaint can use (no romfs or exefs)"
    }
}

private struct ModsBanner: View {
    let text: String
    let symbol: String
    let tint: Color
    let dismiss: () -> Void

    var body: some View {
        HStack(spacing: 10) {
            Image(systemName: symbol).foregroundStyle(tint)
            Text(text).font(.callout).fixedSize(horizontal: false, vertical: true)
            Spacer(minLength: 0)
            Button(action: dismiss) { Image(systemName: "xmark") }
                .buttonStyle(.plain)
                .foregroundStyle(.secondary)
                .accessibilityLabel("Dismiss")
        }
        .padding(.horizontal, 16).padding(.vertical, 12)
        .glassEffect(.regular.tint(tint.opacity(0.18)), in: .rect(cornerRadius: 16))
    }
}
