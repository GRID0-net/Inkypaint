// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import AppKit
import Foundation
import Observation

enum Pane: String, Hashable, CaseIterable, Identifiable {
    case play, online, settings, mods, files
    var id: String { rawValue }
    var title: String {
        switch self {
        case .play: "Play"
        case .online: "Online"
        case .settings: "Settings"
        case .mods: "Mods"
        case .files: "Game Files"
        }
    }
    var symbol: String {
        switch self {
        case .play: "gamecontroller.fill"
        case .online: "person.2.fill"
        case .settings: "slider.horizontal.3"
        case .mods: "puzzlepiece.extension.fill"
        case .files: "externaldrive.fill"
        }
    }
}

/// The launcher's state: where the game is, whether it is ready, and the game while it runs.
@Observable
@MainActor
final class AppModel {
    var pane: Pane = .play
    private(set) var paths: GamePaths
    private(set) var settings: GameSettings
    let preparer = Preparer()
    let online = OnlineModel()
    let mods = ModsModel()
    var sources = GameSources()

    private(set) var running: Process?
    private(set) var playStarted: Date?
    private(set) var lastExit: String?
    private(set) var hideWhilePlaying: Bool

    func setHideWhilePlaying(_ on: Bool) {
        hideWhilePlaying = on
        UserDefaults.standard.set(on, forKey: "hideWhilePlaying")
    }

    var lastPlayed: Date? {
        get { UserDefaults.standard.object(forKey: "lastPlayed") as? Date }
        set { UserDefaults.standard.set(newValue, forKey: "lastPlayed") }
    }

    init() {
        let saved = UserDefaults.standard.string(forKey: "dataFolder").map { URL(fileURLWithPath: $0) }
        let p = GamePaths(data: saved ?? GamePaths.defaultData)
        paths = p
        settings = GameSettings(url: p.settings)
        hideWhilePlaying = UserDefaults.standard.object(forKey: "hideWhilePlaying") as? Bool ?? true
        online.settings = settings
        if !p.isPrepared { pane = .files }
    }

    var isPrepared: Bool { paths.isPrepared }
    var isPlaying: Bool { running != nil }

    /// Uses another data folder: one `inkypaint.sh prepare` filled (the repository's local/data), or a new place for
    /// this app's own preparation.
    func useDataFolder(_ url: URL) {
        paths = GamePaths(data: url)
        settings = GameSettings(url: paths.settings)
        online.settings = settings
        if url == GamePaths.defaultData {
            UserDefaults.standard.removeObject(forKey: "dataFolder")
        } else {
            UserDefaults.standard.set(url.path, forKey: "dataFolder")
        }
    }

    func refreshMods() { mods.refresh(paths, settings) }

    func refresh() {
        settings.reload()
        // a preparation that just finished: the game and its settings are there now
        if case .done = preparer.phase {
            settings = GameSettings(url: paths.settings)
            online.settings = settings
        }
        refreshMods()
    }

    /// The preparation's phase changed: one that just finished compiled in the patches there were
    func preparationChanged() {
        refresh()
        guard case .done = preparer.phase else { return }
        if Grid0Patches.compiledIn(paths) { online.patches.recompiled() }
        // online play on in the game's settings, which the preparation has just made
        if setupOnline, online.isSignedIn { online.setPlaysOnline(true) }
        setupOnline = false
    }

    /// Recompiles the game, for the GRID0+ patches: the extraction is kept, so it is the recompile alone
    func recompile() {
        pane = .files
        preparer.reset()
        prepare()
    }

    func prepare() { preparer.start(sources: sources, paths: paths) }

    // MARK: online play before the preparation

    /// GRID0+'s patches are compiled into the game, so they come before its preparation: a game prepared first and
    /// signed in to afterwards had to be recompiled again for them
    enum SetupStep: Equatable {
        case idle
        case askingOnline       // no account: sign in, or prepare without online play
        case downloadingPatches // signed in: the patches first, then the preparation
    }
    private(set) var setupStep: SetupStep = .idle
    private var setupOnline = false // the preparation under way is for online play

    /// The Prepare button: signed in, the patches and then the preparation; otherwise the question first
    func requestPrepare() {
        guard setupStep == .idle, !preparer.isRunning else { return }
        if online.isSignedIn {
            Task { await prepareWithPatches() }
        } else {
            setupStep = .askingOnline
        }
    }
    /// The question's answers
    func prepareOffline() {
        guard setupStep == .askingOnline else { return }
        setupStep = .idle
        setupOnline = false
        prepare()
    }
    func cancelSetup() {
        if setupStep == .askingOnline { setupStep = .idle }
    }
    /// The newest patches into the game's mods folder, then the preparation, which compiles them in. A download that
    /// fails leaves them to the Online page: the game is prepared all the same, and plays offline until then
    func prepareWithPatches() async {
        guard setupStep != .downloadingPatches, !preparer.isRunning, online.isSignedIn else { return }
        setupStep = .downloadingPatches
        setupOnline = true
        await online.patches.update(paths)
        setupStep = .idle
        prepare()
    }

    // MARK: playing

    func play() {
        guard running == nil, isPrepared else { return }
        lastExit = nil
        do {
            try deployRuntime()
            try FileManager.default.createDirectory(at: GamePaths.logs, withIntermediateDirectories: true)
            let log = GamePaths.logs.appendingPathComponent("game-\(Self.stamp()).log")
            FileManager.default.createFile(atPath: log.path, contents: nil)
            let out = try FileHandle(forWritingTo: log)

            // online, the game signs in to GRID0+ itself: the password goes on its stdin, never on its command line
            // or in the game's settings; the launcher's owner-only credential store keeps it between launches
            let password = online.passwordForGame()
            let input = Pipe()
            let p = Process()
            p.executableURL = paths.executable
            p.currentDirectoryURL = paths.gameDir
            p.environment = Bundled.environment(extra: password == nil ? [:] : ["INKYPAINT_GRID0_STDIN": "1"])
            p.standardInput = input
            p.standardOutput = out
            p.standardError = out
            p.terminationHandler = { [weak self] proc in
                try? out.close()
                let status = proc.terminationStatus, reason = proc.terminationReason
                Task { @MainActor in self?.ended(status: status, reason: reason, log: log) }
            }
            try p.run()
            if let password { input.fileHandleForWriting.write(Data((password + "\n").utf8)) }
            try? input.fileHandleForWriting.close()
            running = p
            playStarted = Date()
            lastPlayed = Date()
            if hideWhilePlaying {
                DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) { [weak self] in
                    if self?.running != nil { NSApp.hide(nil) }
                }
            }
        } catch {
            lastExit = "The game couldn't start: \(error.localizedDescription)"
        }
    }

    func stop() { running?.terminate() }

    private func ended(status: Int32, reason: Process.TerminationReason, log: URL) {
        let ran = playStarted.map { Date().timeIntervalSince($0) } ?? 0
        running = nil
        playStarted = nil
        settings.reload()
        NSApp.unhide(nil)
        NSApp.activate()
        // a game that stops by itself within seconds didn't start: say so, with where to look
        if reason == .uncaughtSignal || (status != 0 && ran < 20) {
            lastExit = "The game stopped unexpectedly (status \(status)). Its log is in \(paths.gameLog.abbreviatedPath)."
        }
    }

    /// The game runs a copy of the runtime named after it (ScarletNX finds the game's code by that name). A newer app
    /// brings a newer runtime: it is copied over the old one before the game starts.
    private func deployRuntime() throws {
        guard let runtime = Bundled.runtime else { return }
        let fm = FileManager.default
        let a = try? fm.attributesOfItem(atPath: runtime.path), b = try? fm.attributesOfItem(atPath: paths.executable.path)
        let same = (a?[.size] as? Int) == (b?[.size] as? Int)
            && ((a?[.modificationDate] as? Date) ?? .distantPast) <= ((b?[.modificationDate] as? Date) ?? .distantPast)
        guard !same else { return }
        let tmp = paths.executable.appendingPathExtension("new")
        try? fm.removeItem(at: tmp)
        try fm.copyItem(at: runtime, to: tmp)
        try fm.setAttributes([.posixPermissions: 0o755], ofItemAtPath: tmp.path)
        _ = try fm.replaceItemAt(paths.executable, withItemAt: tmp)
    }

    private static func stamp() -> String {
        let f = DateFormatter()
        f.dateFormat = "yyyyMMdd-HHmmss"
        return f.string(from: Date())
    }
}
