// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import AppKit
import Foundation

/// The one game inkypaint runs, and the build of it.
enum Game {
    static let name = "Splatoon 3"
    static let titleID = "0100C2500FC20000"
    static let updateID = "0100C2500FC20800"
    static let version = "11.3.0"
}

/// Where a game's files live: ScarletNX's data folder (its `--data`), with the extraction in `work/` and the
/// recompiled game, its settings and its log in `recompiled/<title ID>/`.
struct GamePaths: Equatable {
    var data: URL

    static var appSupport: URL {
        FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("inkypaint", isDirectory: true)
    }
    static var defaultData: URL { appSupport.appendingPathComponent("data", isDirectory: true) }
    static var logs: URL {
        FileManager.default.urls(for: .libraryDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("Logs/inkypaint", isDirectory: true)
    }

    var work: URL { data.appendingPathComponent("work/splatoon3", isDirectory: true) }
    var recompiled: URL { data.appendingPathComponent("recompiled", isDirectory: true) }
    var gameDir: URL { recompiled.appendingPathComponent(Game.titleID, isDirectory: true) }
    var executable: URL { gameDir.appendingPathComponent(Game.titleID) }
    var settings: URL { gameDir.appendingPathComponent("\(Game.titleID).ini") }
    var gameLog: URL { gameDir.appendingPathComponent("\(Game.titleID).log") }

    /// The extraction is whole: the executable and the RomFS are both there.
    var isExtracted: Bool {
        FileManager.default.fileExists(atPath: work.appendingPathComponent("exefs/main").path)
            && FileManager.default.fileExists(atPath: work.appendingPathComponent("romfs").path)
    }
    /// The recompile finished: the runtime is deployed last, so its copy marks a whole recompile.
    var isPrepared: Bool { FileManager.default.isExecutableFile(atPath: executable.path) }

    /// A data folder from `inkypaint.sh prepare` (the repository's `local/data`) or this app's own.
    static func looksPrepared(_ data: URL) -> Bool { GamePaths(data: data).isPrepared }
}

/// What the app carries inside itself (scripts/package-launcher.sh puts it there). Running from Xcode or `swift run`,
/// the environment points at a build instead: INKYPAINT_RUNTIME (build/macos/scarletnx) and INKYPAINT_MOLTENVK.
enum Bundled {
    static var runtime: URL? {
        if let path = ProcessInfo.processInfo.environment["INKYPAINT_RUNTIME"] { return URL(fileURLWithPath: path) }
        return Bundle.main.url(forAuxiliaryExecutable: "scarletnx")
    }
    static var frameworks: URL? { Bundle.main.privateFrameworksURL }
    static var moltenVK: URL? {
        if let path = ProcessInfo.processInfo.environment["INKYPAINT_MOLTENVK"] { return URL(fileURLWithPath: path) }
        guard let url = frameworks?.appendingPathComponent("libMoltenVK.dylib"),
              FileManager.default.fileExists(atPath: url.path) else { return nil }
        return url
    }
    static var isComplete: Bool { runtime.map { FileManager.default.isExecutableFile(atPath: $0.path) } == true && moltenVK != nil }

    /// The environment the runtime runs in. The game's copy of the runtime lives in the data folder, away from the
    /// app's Frameworks, so the dynamic loader is told where LLVM is; SDL is told where Vulkan (MoltenVK) is.
    static func environment(extra: [String: String] = [:]) -> [String: String] {
        var env = ProcessInfo.processInfo.environment
        if let frameworks, FileManager.default.fileExists(atPath: frameworks.path) {
            env["DYLD_LIBRARY_PATH"] = frameworks.path
        }
        if let moltenVK { env["SDL_VULKAN_LIBRARY"] = moltenVK.path }
        env["MVK_CONFIG_LOG_LEVEL"] = env["MVK_CONFIG_LOG_LEVEL"] ?? "2"
        extra.forEach { env[$0.key] = $0.value }
        return env
    }
}

extension URL {
    /// Bytes free for large files on this URL's volume.
    var freeSpace: Int64? {
        let values = try? resourceValues(forKeys: [.volumeAvailableCapacityForImportantUsageKey])
        return values?.volumeAvailableCapacityForImportantUsage
    }
    var abbreviatedPath: String { (path as NSString).abbreviatingWithTildeInPath }
    func revealInFinder() { NSWorkspace.shared.activateFileViewerSelecting([self]) }
}
