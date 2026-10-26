// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import AppKit
import Foundation
import Observation

enum Pane: String, Hashable, CaseIterable, Identifiable {
    case play, settings, files
    var id: String { rawValue }
    var title: String {
        switch self {
        case .play: "Play"
        case .settings: "Settings"
        case .files: "Game Files"
        }
    }
    var symbol: String {
        switch self {
        case .play: "gamecontroller.fill"
        case .settings: "slider.horizontal.3"
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
        if !p.isPrepared { pane = .files }
    }

    var isPrepared: Bool { paths.isPrepared }
    var isPlaying: Bool { running != nil }

    /// Uses another data folder: one `inkypaint.sh prepare` filled (the repository's local/data), or a new place for
    /// this app's own preparation.
    func useDataFolder(_ url: URL) {
        paths = GamePaths(data: url)
        settings = GameSettings(url: paths.settings)
        if url == GamePaths.defaultData {
            UserDefaults.standard.removeObject(forKey: "dataFolder")
        } else {
            UserDefaults.standard.set(url.path, forKey: "dataFolder")
        }
    }

    func refresh() {
        settings.reload()
        // a preparation that just finished: the game and its settings are there now
        if case .done = preparer.phase { settings = GameSettings(url: paths.settings) }
    }

    func prepare() { preparer.start(sources: sources, paths: paths) }

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

            let p = Process()
            p.executableURL = paths.executable
            p.currentDirectoryURL = paths.gameDir
            p.environment = Bundled.environment()
            p.standardOutput = out
            p.standardError = out
            p.terminationHandler = { [weak self] proc in
                try? out.close()
                let status = proc.terminationStatus, reason = proc.terminationReason
                Task { @MainActor in self?.ended(status: status, reason: reason, log: log) }
            }
            try p.run()
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
