// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import Foundation
import Observation

/// The files a preparation starts from: the base game's and its 11.3.0 update's NSPs, and the console's keys.
struct GameSources: Equatable {
    var base: URL?
    var update: URL?
    var prodKeys: URL?
    var titleKeys: URL?

    var isComplete: Bool { base != nil && update != nil && prodKeys != nil }

    /// Fills in what a folder holds, the way `inkypaint.sh prepare` finds it: the NSPs by their title IDs, the keys by
    /// name, in the folder or one level down ("the rom/", "decryption keys/").
    mutating func scan(folder: URL) {
        let fm = FileManager.default
        var files: [URL] = []
        let top = (try? fm.contentsOfDirectory(at: folder, includingPropertiesForKeys: [.isDirectoryKey])) ?? []
        for url in top {
            if (try? url.resourceValues(forKeys: [.isDirectoryKey]).isDirectory) == true {
                files += (try? fm.contentsOfDirectory(at: url, includingPropertiesForKeys: nil)) ?? []
            } else {
                files.append(url)
            }
        }
        for url in files { take(url) }
    }

    /// One file, wherever it fits.
    mutating func take(_ url: URL) {
        let name = url.lastPathComponent.lowercased()
        if name == "prod.keys" { prodKeys = url }
        else if name == "title.keys" { titleKeys = url }
        else if url.pathExtension.lowercased() == "nsp" {
            if name.contains(Game.updateID.lowercased()) || name.contains("[upd]") { update = url }
            else if name.contains(Game.titleID.lowercased()) || name.contains("[base]") { base = url }
        }
    }
}

/// Extracts the game with its update and recompiles it, by running the app's runtime as `inkypaint.sh prepare`
/// does, and follows its progress from what it prints.
@Observable
@MainActor
final class Preparer {
    enum Phase: Equatable {
        case idle, extracting, recompiling, installingDlc, done
        case failed(String)
    }

    private(set) var phase: Phase = .idle
    private(set) var extractFraction = 0.0
    private(set) var compileFraction = 0.0
    private(set) var detail = ""
    private(set) var log: [String] = []
    private(set) var started: Date?
    private var process: Process?
    private var cancelled = false
    private(set) var installingDlc = false
    private var dlcArgs: [String] = []
    private var dlcPaths: GamePaths?

    var isRunning: Bool { phase == .extracting || phase == .recompiling || phase == .installingDlc }
    /// The whole job: extraction is the first quarter, the recompile the rest (on an M2 it takes about 30 minutes).
    var fraction: Double { 0.25 * extractFraction + 0.75 * compileFraction }

    /// Parallel compiles: two on an 8 GB Mac, which otherwise swaps; with more memory, one for each 2 GB past 4 GB,
    /// up to one for each core (giant functions are compiled at O1, which used to take the most memory).
    static var jobs: Int {
        let gb = Int(ProcessInfo.processInfo.physicalMemory >> 30)
        if gb <= 8 { return 2 }
        return max(2, min(ProcessInfo.processInfo.activeProcessorCount, (gb - 4) / 2))
    }

    func start(sources: GameSources, paths: GamePaths) {
        guard !isRunning else { return }
        guard let runtime = Bundled.runtime else {
            phase = .failed("This copy of inkypaint doesn't contain the runtime. Build the app with ./inkypaint.sh launcher.")
            return
        }
        cancelled = false
        installingDlc = false
        log = []
        started = Date()
        extractFraction = 0
        compileFraction = 0
        Task { await run(runtime: runtime, sources: sources, paths: paths) }
    }

    func cancel() {
        cancelled = true
        process?.terminate()
    }

    func reset() {
        if !isRunning { phase = .idle; installingDlc = false }
    }

    func installDlc(_ file: URL, sources: GameSources, paths: GamePaths) {
        guard !isRunning else { return }
        installingDlc = true
        dlcArgs = []
        guard paths.isPrepared, Bundled.runtime != nil else {
            phase = .failed("Prepare the base game and install the runtime first.")
            return
        }
        dlcPaths = paths
        dlcArgs = ["--data", paths.data.path, "--install-dlc", file.path, "--outdir", paths.recompiled.path]
        if let keys = sources.prodKeys { dlcArgs += ["--keys", keys.path] }
        if let keys = sources.titleKeys { dlcArgs += ["--title-keys", keys.path] }
        retryDlc()
    }

    func retryDlc() {
        guard !isRunning, !dlcArgs.isEmpty, let paths = dlcPaths, let runtime = Bundled.runtime else { return }
        cancelled = false
        log = []
        started = Date()
        extractFraction = 0
        compileFraction = 0
        phase = .installingDlc
        detail = "Installing optional DLC; the base game is kept"
        let args = dlcArgs
        Task {
            do {
                try await execute(runtime, args)
                extractFraction = 1
                compileFraction = 1
                phase = paths.isPrepared ? .done : .failed("The base game is no longer available.")
                detail = "DLC installed"
            } catch {
                phase = .failed(cancelled ? "DLC installation stopped." : error.localizedDescription)
            }
            process = nil
        }
    }

    private func run(runtime: URL, sources: GameSources, paths: GamePaths) async {
        let data = paths.data.path
        do {
            try FileManager.default.createDirectory(at: paths.data, withIntermediateDirectories: true)
            if paths.isExtracted {
                append("Already extracted: \(paths.work.abbreviatedPath)")
                extractFraction = 1
            } else {
                guard let base = sources.base, let update = sources.update, let keys = sources.prodKeys else {
                    phase = .failed("Choose the game, its update and your prod.keys first.")
                    return
                }
                phase = .extracting
                detail = "Reading the game"
                var args = ["--data", data, "-x", base.path, "--update", update.path, "--keys", keys.path]
                if let titleKeys = sources.titleKeys { args += ["--title-keys", titleKeys.path] }
                args += ["--out", paths.work.path]
                try await execute(runtime, args)
            }
            phase = .recompiling
            detail = "Translating the game for Apple silicon"
            try await execute(runtime, ["--data", data, "-r", paths.work.path, "--outdir", paths.recompiled.path,
                                        "--jobs", String(Self.jobs), "-O", "2"])
            compileFraction = 1
            phase = paths.isPrepared ? .done : .failed("The recompile ended without making the game. See the details.")
        } catch {
            phase = .failed(cancelled ? "Stopped. Preparing again picks up where it stopped." : error.localizedDescription)
        }
        process = nil
    }

    struct Failure: LocalizedError {
        let status: Int32
        let last: String
        var errorDescription: String? {
            last.isEmpty ? "The runtime stopped with status \(status)." : last
        }
    }

    /// Runs the runtime with args, reading its output as it comes; throws when it fails.
    private func execute(_ runtime: URL, _ args: [String]) async throws {
        let p = Process()
        p.executableURL = runtime
        p.arguments = args
        p.environment = Bundled.environment()
        let pipe = Pipe()
        p.standardOutput = pipe
        p.standardError = pipe
        process = p
        append("$ scarletnx " + args.joined(separator: " "))

        let status: Int32 = try await withCheckedThrowingContinuation { cont in
            pipe.fileHandleForReading.readabilityHandler = { [weak self] handle in
                let chunk = handle.availableData
                guard !chunk.isEmpty, let text = String(data: chunk, encoding: .utf8) else { return }
                Task { @MainActor in self?.consume(text) }
            }
            p.terminationHandler = { proc in
                pipe.fileHandleForReading.readabilityHandler = nil
                cont.resume(returning: proc.terminationStatus)
            }
            do { try p.run() } catch {
                pipe.fileHandleForReading.readabilityHandler = nil
                cont.resume(throwing: error)
            }
        }
        try await Task.sleep(for: .milliseconds(100)) // the last output lands
        if status != 0 || cancelled {
            let last = log.last(where: { $0.lowercased().contains("error") || $0.lowercased().contains("cannot") }) ?? ""
            throw Failure(status: status, last: last)
        }
    }

    private var partial = ""
    /// The runtime's output in rows: "\r" ends the extraction's progress rows, which rewrite one line in a terminal.
    private func consume(_ text: String) {
        partial += text
        var rows = partial.components(separatedBy: CharacterSet(charactersIn: "\r\n"))
        partial = rows.removeLast()
        for row in rows where !row.trimmingCharacters(in: .whitespaces).isEmpty { parse(row) }
    }

    private func parse(_ row: String) {
        let t = row.trimmingCharacters(in: .whitespaces)
        // "extracted 1229 of 3164 MB (38%)"
        if t.hasPrefix("extracted "), let n = numbers(t), n.count >= 2, n[1] > 0 {
            extractFraction = min(1, Double(n[0]) / Double(n[1]))
            detail = "Extracted \(n[0]) of \(n[1]) MB"
            if log.last?.hasPrefix("  extracted ") == true { log[log.count - 1] = row; return }
        // "compiled block 37/246: code_36, ..."
        } else if t.hasPrefix("compiled block "), let n = numbers(t), n.count >= 2, n[1] > 0 {
            compileFraction = min(1, Double(n[0]) / Double(n[1]))
            detail = "Compiled \(n[0]) of \(n[1]) blocks"
        } else if t.hasPrefix("Recompiling ") {
            detail = "Translating the game for Apple silicon"
        } else if t.hasPrefix("Built ") {
            detail = "Done"
        }
        append(row)
    }

    private func append(_ row: String) {
        log.append(row)
        if log.count > 400 { log.removeFirst(log.count - 400) }
    }

    /// The whole numbers in a row, in order.
    private func numbers(_ s: String) -> [Int]? {
        var out: [Int] = []
        var digits = ""
        for c in s {
            if c.isASCII, c.isNumber { digits.append(c) }
            else if !digits.isEmpty { out.append(Int(digits) ?? 0); digits = "" }
        }
        if !digits.isEmpty { out.append(Int(digits) ?? 0) }
        return out.isEmpty ? nil : out
    }
}
