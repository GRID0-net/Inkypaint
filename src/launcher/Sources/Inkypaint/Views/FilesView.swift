// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import SwiftUI
import UniformTypeIdentifiers

/// Setting the game up: the dumps and keys to start from, where it goes, then extracting and recompiling it. Once it
/// is ready, where it is and what can be done with it.
struct FilesView: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                hero
                if model.preparer.isRunning || model.preparer.phase != .idle {
                    PrepareProgress()
                } else if model.isPrepared {
                    ReadyCard()
                } else {
                    SetupSteps()
                }
            }
            .padding(28)
            .frame(maxWidth: 760)
            .frame(maxWidth: .infinity)
        }
        .background { SetupBackdrop() }
        .navigationTitle("Game Files")
    }

    private var hero: some View {
        HStack(alignment: .center, spacing: 16) {
            SplatMark(splat: .burst, color: Ink.purple, size: 74, angle: -12)
                .overlay { Image(systemName: "externaldrive.fill").font(.title2).foregroundStyle(.white) }
            VStack(alignment: .leading, spacing: 4) {
                Text(model.isPrepared ? "Splatoon 3 is ready" : "Set up Splatoon 3")
                    .font(.inkTitle(30))
                Text(model.isPrepared
                     ? "Recompiled for Apple silicon, from your own copy of the game."
                     : "inkypaint turns your copy of the game into an app for this Mac. You need the game, its 11.3.0 update and your console's keys.")
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
        .padding(.bottom, 4)
    }
}

/// A quieter backdrop than Play's: two splats in the corners, under everything.
private struct SetupBackdrop: View {
    var body: some View {
        GeometryReader { geo in
            ZStack {
                SplatMark(splat: .streak, color: Ink.purple.opacity(0.18), size: 380, angle: 20)
                    .position(x: geo.size.width - 60, y: 70)
                SplatMark(splat: .drip, color: Ink.lime.opacity(0.14), size: 260, angle: -10)
                    .position(x: 40, y: geo.size.height - 40)
            }
        }
        .ignoresSafeArea()
    }
}

// MARK: - setup

private struct SetupSteps: View {
    @Environment(AppModel.self) private var model
    @State private var importing: Import?
    @State private var pending: Import? // what was asked for, kept past the panel's dismissal
    @State private var dataMessage: String?

    enum Slot { case base, update, prodKeys, titleKeys }
    /// What the open panel is choosing: one file picker serves them all (two on one view would fight)
    enum Import {
        case file(Slot), scanFolder, dataFolder
        var types: [UTType] {
            switch self {
            case .file: [.data]
            case .scanFolder, .dataFolder: [.folder]
            }
        }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            GlassCard {
                VStack(alignment: .leading, spacing: 14) {
                    stepTitle(1, "The game", done: model.sources.base != nil && model.sources.update != nil)
                    FileRow(title: "Splatoon 3", hint: "The base game's NSP (0100C2500FC20000)", url: model.sources.base) {
                        ask(.file(.base))
                    }
                    FileRow(title: "Update 11.3.0", hint: "The update's NSP (0100C2500FC20800, v2752512)",
                            url: model.sources.update) { ask(.file(.update)) }
                }
            }
            GlassCard {
                VStack(alignment: .leading, spacing: 14) {
                    stepTitle(2, "Your keys", done: model.sources.prodKeys != nil)
                    FileRow(title: "prod.keys", hint: "Dumped from your console", url: model.sources.prodKeys) {
                        ask(.file(.prodKeys))
                    }
                    FileRow(title: "title.keys", hint: "Optional", url: model.sources.titleKeys) {
                        ask(.file(.titleKeys))
                    }
                }
            }
            GlassCard {
                VStack(alignment: .leading, spacing: 10) {
                    stepTitle(3, "Where it goes", done: true)
                    HStack {
                        Image(systemName: "folder.fill").foregroundStyle(Ink.purple)
                        Text(model.paths.data.abbreviatedPath).font(.callout.monospaced()).lineLimit(1)
                            .truncationMode(.middle)
                        Spacer()
                        Button("Change…") { ask(.dataFolder) }
                            .buttonStyle(.glass)
                    }
                    Text(spaceLine).font(.caption).foregroundStyle(.secondary)
                }
            }

            HStack(spacing: 12) {
                Button {
                    model.prepare()
                } label: {
                    Label("Prepare Splatoon 3", systemImage: "wand.and.stars")
                        .font(.title3.weight(.bold))
                        .padding(.horizontal, 10).padding(.vertical, 4)
                }
                .buttonStyle(.glassProminent)
                .tint(Ink.purple)
                .controlSize(.extraLarge)
                .disabled(!model.sources.isComplete && !model.paths.isExtracted)

                Button("Find in a Folder…") { ask(.scanFolder) }
                    .buttonStyle(.glass)
                    .controlSize(.large)
                Spacer()
            }
            Text("About 30 minutes on an M2, and 13 GB of disk. The Mac stays usable meanwhile.")
                .font(.caption)
                .foregroundStyle(.secondary)

            Divider().padding(.vertical, 6)
            VStack(alignment: .leading, spacing: 6) {
                Text("Prepared the game with inkypaint.sh already?").font(.headline)
                Text("Choose its data folder (local/data in your inkypaint checkout) to play it from here.")
                    .font(.callout).foregroundStyle(.secondary)
                Button("Use an Existing Data Folder…") { ask(.dataFolder) }
                    .buttonStyle(.glass)
                if let dataMessage { Text(dataMessage).font(.callout).foregroundStyle(.orange) }
            }
        }
        .dropDestination(for: URL.self) { urls, _ in
            for url in urls {
                if url.hasDirectoryPath { model.sources.scan(folder: url) } else { model.sources.take(url) }
            }
            return true
        }
        .fileImporter(isPresented: Binding(get: { importing != nil }, set: { if !$0 { importing = nil } }),
                      allowedContentTypes: importing?.types ?? [.data]) { result in
            guard case .success(let url) = result, let what = pending else { return }
            pending = nil
            switch what {
            case .file(.base): model.sources.base = url
            case .file(.update): model.sources.update = url
            case .file(.prodKeys): model.sources.prodKeys = url
            case .file(.titleKeys): model.sources.titleKeys = url
            case .scanFolder: model.sources.scan(folder: url)
            case .dataFolder:
                model.useDataFolder(url)
                dataMessage = model.isPrepared ? nil : "No prepared game there yet: preparing puts it in this folder."
            }
        }
    }

    private func ask(_ what: Import) {
        pending = what
        importing = what
    }

    private var spaceLine: String {
        let free = (model.paths.data.freeSpace ?? GamePaths.appSupport.deletingLastPathComponent().freeSpace)
        guard let free else { return "Needs about 13 GB." }
        let fmt = ByteCountFormatter.string(fromByteCount: free, countStyle: .file)
        return "Needs about 13 GB, \(fmt) free."
    }

    private func stepTitle(_ n: Int, _ title: String, done: Bool) -> some View {
        HStack(spacing: 12) {
            StepBadge(number: n, done: done)
            Text(title).font(.title3.weight(.bold))
        }
    }
}

private struct FileRow: View {
    var title: String
    var hint: String
    var url: URL?
    var choose: () -> Void

    var body: some View {
        HStack(spacing: 12) {
            Image(systemName: url == nil ? "doc.badge.plus" : "checkmark.circle.fill")
                .font(.title3)
                .foregroundStyle(url == nil ? AnyShapeStyle(.secondary) : AnyShapeStyle(Ink.lime))
                .frame(width: 26)
            VStack(alignment: .leading, spacing: 2) {
                Text(title).font(.body.weight(.semibold))
                Text(url?.lastPathComponent ?? hint)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
                    .truncationMode(.middle)
            }
            Spacer()
            Button(url == nil ? "Choose…" : "Change…", action: choose)
                .buttonStyle(.glass)
        }
    }
}

// MARK: - preparing

private struct PrepareProgress: View {
    @Environment(AppModel.self) private var model
    @State private var showLog = false

    var body: some View {
        let p = model.preparer
        VStack(alignment: .leading, spacing: 14) {
            GlassCard(tint: tint) {
                VStack(alignment: .leading, spacing: 14) {
                    HStack(spacing: 12) {
                        StepBadge(number: p.phase == .recompiling ? 2 : 1, color: Ink.magenta,
                                  done: p.phase == .done)
                        VStack(alignment: .leading, spacing: 2) {
                            Text(title).font(.title3.weight(.bold))
                            Text(p.detail).font(.callout).foregroundStyle(.secondary)
                        }
                        Spacer()
                        if p.isRunning {
                            Text(p.fraction, format: .percent.precision(.fractionLength(0)))
                                .font(.inkTitle(26))
                                .monospacedDigit()
                        }
                    }
                    if p.isRunning {
                        ProgressView(value: p.fraction)
                            .tint(Ink.lime)
                        if let started = p.started {
                            Text("Started \(started.formatted(date: .omitted, time: .shortened)). You can leave this running.")
                                .font(.caption).foregroundStyle(.secondary)
                        }
                    }
                    if case .failed(let why) = p.phase {
                        Text(why).foregroundStyle(.orange).textSelection(.enabled)
                    }
                    HStack {
                        if p.isRunning {
                            Button("Stop", role: .cancel) { p.cancel() }.buttonStyle(.glass)
                        } else if p.phase == .done {
                            Button {
                                p.reset()
                                model.refresh()
                                model.pane = .play
                            } label: {
                                Label("Play Splatoon 3", systemImage: "play.fill").font(.headline).foregroundStyle(.black)
                            }
                            .buttonStyle(.glassProminent)
                            .tint(Ink.lime)
                            .controlSize(.large)
                        } else {
                            Button("Try Again") { model.prepare() }.buttonStyle(.glassProminent).tint(Ink.purple)
                            Button("Back") { p.reset() }.buttonStyle(.glass)
                        }
                        Spacer()
                        Toggle("Details", isOn: $showLog).toggleStyle(.button).buttonStyle(.glass)
                    }
                }
            }
            if showLog { LogConsole(lines: p.log) }
        }
    }

    private var tint: Color? {
        switch model.preparer.phase {
        case .done: Ink.lime
        case .failed: .orange
        default: nil
        }
    }

    private var title: String {
        switch model.preparer.phase {
        case .idle: "Getting ready"
        case .extracting: "Extracting the game"
        case .recompiling: "Recompiling for Apple silicon"
        case .done: "All done"
        case .failed: "Preparing stopped"
        }
    }
}

private struct LogConsole: View {
    var lines: [String]

    var body: some View {
        ScrollViewReader { proxy in
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 1) {
                    ForEach(lines.indices, id: \.self) { i in
                        Text(lines[i]).font(.caption.monospaced()).textSelection(.enabled).id(i)
                    }
                }
                .padding(12)
                .frame(maxWidth: .infinity, alignment: .leading)
            }
            .frame(height: 220)
            .background(.black.opacity(0.35), in: .rect(cornerRadius: 16))
            .onChange(of: lines.count) { proxy.scrollTo(lines.count - 1, anchor: .bottom) }
        }
    }
}

// MARK: - ready

private struct ReadyCard: View {
    @Environment(AppModel.self) private var model
    @State private var choosingData = false
    @State private var confirmRecompile = false

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            GlassCard(tint: Ink.lime) {
                VStack(alignment: .leading, spacing: 12) {
                    LabeledContent("Game", value: "\(Game.name) \(Game.version)")
                    LabeledContent("Data folder") {
                        Text(model.paths.data.abbreviatedPath).font(.callout.monospaced()).lineLimit(1)
                            .truncationMode(.middle)
                    }
                    HStack {
                        Button("Show in Finder") { model.paths.gameDir.revealInFinder() }.buttonStyle(.glass)
                        Button("Show Game Log") { model.paths.gameLog.revealInFinder() }.buttonStyle(.glass)
                        Spacer()
                    }
                }
            }
            GlassCard {
                VStack(alignment: .leading, spacing: 10) {
                    Text("Recompile").font(.headline)
                    Text("After an update of inkypaint that changes how games are translated, the game is recompiled from its extraction. About 25 minutes.")
                        .font(.callout).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
                    HStack {
                        Button("Recompile…") { confirmRecompile = true }.buttonStyle(.glass)
                            .disabled(!model.paths.isExtracted)
                        Button("Use Another Data Folder…") { choosingData = true }.buttonStyle(.glass)
                    }
                }
            }
        }
        .fileImporter(isPresented: $choosingData, allowedContentTypes: [.folder]) { result in
            if case .success(let url) = result { model.useDataFolder(url) }
        }
        .confirmationDialog("Recompile Splatoon 3?", isPresented: $confirmRecompile) {
            Button("Recompile") { model.prepare() }
        } message: {
            Text("The game can't be played until it finishes.")
        }
    }
}
