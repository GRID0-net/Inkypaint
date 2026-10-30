// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import Foundation
import Observation

/// The game's mods on disk, as the runtime reads them (src/common/utilities/mods.c): each a folder of
/// <game>_data/mods/, with a romfs/ laid over the game's files when it starts, and an exefs/ (replacement modules or
/// .ips/.pchtxt patches) that only a recompile puts into the game's code. The Qt launcher's modfiles.cpp.
enum ModFiles {
    private static var fm: FileManager { .default }

    /// strcmp's order, which the runtime sorts by
    private static func byBytes(_ a: String, _ b: String) -> Bool { Array(a.utf8).lexicographicallyPrecedes(Array(b.utf8)) }
    private static func entries(_ folder: URL) -> [String] { (try? fm.contentsOfDirectory(atPath: folder.path)) ?? [] }
    private static func isLink(_ url: URL) -> Bool {
        (try? fm.attributesOfItem(atPath: url.path)[.type] as? FileAttributeType) == .typeSymbolicLink
    }
    private static func isDir(_ url: URL) -> Bool {
        var dir: ObjCBool = false
        return fm.fileExists(atPath: url.path, isDirectory: &dir) && dir.boolValue && !isLink(url)
    }
    private static func isFile(_ url: URL) -> Bool {
        var dir: ObjCBool = false
        return fm.fileExists(atPath: url.path, isDirectory: &dir) && !dir.boolValue
    }
    /// snx_mods_exefs_file: a module of the program (rtld, main, sdk, subsdk<n>) or main.npdm
    private static func exefsFile(_ name: String) -> Bool {
        if ["rtld", "main", "sdk", "main.npdm"].contains(name) { return true }
        guard name.hasPrefix("subsdk"), name.count > 6 else { return false }
        return name.dropFirst(6).allSatisfy { $0.isASCII && $0.isNumber }
    }
    private static func patchFile(_ name: String) -> Bool {
        let lower = name.lowercased()
        return !name.hasPrefix(".") && (lower.hasSuffix(".ips") || lower.hasSuffix(".pchtxt"))
    }

    /// The mods in folder, sorted as the runtime lays them: no files, hidden folders or links
    static func list(_ folder: URL) -> [String] {
        entries(folder).filter { !$0.hasPrefix(".") && isDir(folder.appendingPathComponent($0)) }.sorted(by: byBytes)
    }
    static func isOff(_ off: String, _ name: String) -> Bool { off.split(separator: "/", omittingEmptySubsequences: false).contains { $0 == name } }
    static func setOff(_ off: String, _ name: String, _ isOff: Bool) -> String {
        var names = off.split(separator: "/").map(String.init).filter { $0 != name }
        if isOff { names.append(name) }
        return names.joined(separator: "/")
    }
    private static func sdCompanions(_ root: URL, within boundary: URL) -> [URL] {
        let contents = root.deletingLastPathComponent(), atmosphere = root.deletingLastPathComponent().deletingLastPathComponent()
        let base: URL
        if contents.lastPathComponent.lowercased() == "contents" && atmosphere.lastPathComponent.lowercased() == "atmosphere" {
            base = atmosphere.deletingLastPathComponent()
        } else if isDir(root.appendingPathComponent("flexlion3")) { base = root }
        else { return [] }
        let limit = boundary.standardizedFileURL.path
        guard base.path != "/", base.path == limit || base.path.hasPrefix(limit + "/") else { return [] }
        let excluded = ["atmosphere", "contents", "romfs", "exefs", "sdmc", "nintendo", "__macosx"]
        return entries(base).filter { !$0.hasPrefix(".") && !excluded.contains($0.lowercased()) && isDir(base.appendingPathComponent($0)) }.map { base.appendingPathComponent($0) }
    }
    static func hasSdmc(_ mod: URL) -> Bool { isDir(mod.appendingPathComponent("sdmc")) }
    static func hasRomfs(_ mod: URL) -> Bool { isDir(mod.appendingPathComponent("romfs")) }
    static func hasCode(_ mod: URL) -> Bool {
        let exefs = mod.appendingPathComponent("exefs")
        guard isDir(exefs) else { return false }
        return entries(exefs).contains { (exefsFile($0) || patchFile($0)) && isFile(exefs.appendingPathComponent($0)) }
    }

    /// snx_mods_stamp's value for the mods played: 16 hex digits, "" when none changes the code. The recompile writes
    /// it into the game's <id>.abi after " mods ", so a difference means the code mods changed since
    static func stamp(_ folder: URL, off: String) -> String {
        let exefs = list(folder).filter { !isOff(off, $0) }.map { folder.appendingPathComponent($0).appendingPathComponent("exefs") }
            .filter(isDir)
        var files: [(name: String, path: URL)] = []
        for dir in exefs {
            for name in entries(dir) where exefsFile(name) && isFile(dir.appendingPathComponent(name)) {
                if let at = files.firstIndex(where: { $0.name == name }) { files[at].path = dir.appendingPathComponent(name) }
                else { files.append((name, dir.appendingPathComponent(name))) }
            }
        }
        files.sort { byBytes($0.name, $1.name) }
        var h: UInt64 = 0xcbf29ce484222325
        var count = 0
        func hash(_ bytes: some Sequence<UInt8>) { for b in bytes { h = (h ^ UInt64(b)) &* 0x100000001b3 } }
        func hashFile(_ name: String, _ path: URL) {
            let data = (try? Data(contentsOf: path)) ?? Data()
            hash(Array(name.utf8) + [0])
            hash((0..<8).map { UInt8(truncatingIfNeeded: UInt64(data.count) >> (8 * UInt64($0))) })
            hash(data)
            count += 1
        }
        for f in files { hashFile(f.name, f.path) }
        for dir in exefs {
            for name in entries(dir).sorted(by: byBytes) where patchFile(name) && isFile(dir.appendingPathComponent(name)) {
                hashFile(name, dir.appendingPathComponent(name))
            }
        }
        return count > 0 ? String(format: "%016llx", h) : ""
    }
    /// The stamp the game was last recompiled with ("" for none)
    static func recorded(_ abi: URL) -> String {
        guard let text = try? String(contentsOf: abi, encoding: .utf8), let at = text.range(of: " mods ") else { return "" }
        return String(text[at.upperBound...])
    }

    /// Where a download keeps its mods: every folder under base that holds a romfs/ or an exefs/ (the folder itself,
    /// ModName/romfs, atmosphere/contents/<title ID>/romfs...), shallowest first, none inside another
    static func roots(_ base: URL) -> [URL] {
        if ["romfs", "exefs", "sdmc"].contains(base.lastPathComponent.lowercased()), isDir(base) { return [base.deletingLastPathComponent()] }
        var found: [URL] = [], queue: [(URL, Int)] = [(base, 0)], i = 0
        while i < queue.count && i < 4096 {
            let (dir, depth) = queue[i]
            i += 1
            let children = entries(dir).sorted(by: byBytes)
            if children.contains(where: { ["romfs", "exefs", "sdmc"].contains($0.lowercased()) && isDir(dir.appendingPathComponent($0)) }) {
                found.append(dir)
                continue
            }
            guard depth < 16 else { continue }
            for c in children where c != "__MACOSX" && !c.hasPrefix(".") && isDir(dir.appendingPathComponent(c)) {
                queue.append((dir.appendingPathComponent(c), depth + 1))
            }
        }
        return found
    }
    /// A mod's folder name from a download's: no separators, no leading dot, a title ID folder replaced by fallback,
    /// and unused in folder
    static func freeName(_ folder: URL, wanted: String, fallback: String) -> String {
        var name = wanted
        let isTitleId = name.count == 16 && name.allSatisfy(\.isHexDigit)
        if isTitleId || ["romfs", "contents", "atmosphere"].contains(name.lowercased()) { name = fallback }
        name = String(name.map { "/\\:*?\"<>|".contains($0) ? "-" : $0 })
        while name.hasPrefix(".") { name.removeFirst() }
        name = name.trimmingCharacters(in: .whitespaces)
        if name.isEmpty { name = "Mod" }
        var free = name, n = 2
        while fm.fileExists(atPath: folder.appendingPathComponent(free).path) { free = "\(name) \(n)"; n += 1 }
        return free
    }
    /// Promote a single nested installed payload without deleting its wrapper or overwriting files.
    static func normalizeInstalled(_ mod: URL) -> String? {
        let payloads = hasRomfs(mod) || hasCode(mod) || hasSdmc(mod) ? [mod] : roots(mod)
        guard payloads.count == 1 else { return "No usable romfs/exefs at \(mod.path). Re-add the original folder or archive." }
        let root = payloads[0].standardizedFileURL, base = mod.standardizedFileURL
        guard root.path == base.path || root.path.hasPrefix(base.path + "/") else { return "The mod payload is outside its installed folder." }
        let companions = sdCompanions(root, within: base)
        do {
            for part in entries(root) where ["romfs", "exefs", "sdmc"].contains(part.lowercased()) {
                let source = root.appendingPathComponent(part), target = base.appendingPathComponent(part.lowercased())
                if source.path == target.path || FileManager.default.fileExists(atPath: target.path) { continue }
                guard isDir(source) else { return "The mod payload is a link. Add the original folder instead." }
                try FileManager.default.moveItem(at: source, to: target)
            }
            for companion in companions {
                let sd = base.appendingPathComponent("sdmc"), target = sd.appendingPathComponent(companion.lastPathComponent)
                try fm.createDirectory(at: sd, withIntermediateDirectories: true)
                guard !fm.fileExists(atPath: target.path) else { return "Duplicate SD companion folder: \(target.path)." }
                try fm.moveItem(at: companion, to: target)
            }
        } catch { return "Couldn't normalize mod folders: \(error.localizedDescription)" }
        return hasRomfs(mod) || hasCode(mod) || hasSdmc(mod) ? nil : "No supported files at \(mod.path)."
    }

    private static func copyPayload(_ source: URL, _ destination: URL) throws {
        let from = source.resolvingSymlinksInPath().standardizedFileURL.path
        let to = destination.resolvingSymlinksInPath().standardizedFileURL.path
        guard isDir(source), from != to, !to.hasPrefix(from + "/") else {
            throw NSError(domain: "Inkypaint.Mods", code: 2, userInfo: [NSLocalizedDescriptionKey: "The mod source is a link or contains the installation folder. Add the original bundle from another folder."])
        }
        try fm.copyItem(at: source, to: destination)
    }

    /// Copies root's romfs/ and exefs/ (whatever their case) into the new mod folder to, through a hidden folder
    /// renamed at the end, so the runtime never sees half a mod
    static func install(_ root: URL, to: URL, packageBase: URL? = nil) throws {
        let boundary = packageBase ?? root
        let onePart = ["romfs", "exefs", "sdmc"].contains(boundary.lastPathComponent.lowercased()) && boundary.deletingLastPathComponent().standardizedFileURL == root.standardizedFileURL
        let staging = to.deletingLastPathComponent().appendingPathComponent(".installing-\(to.lastPathComponent)")
        try? fm.removeItem(at: staging)
        do {
            try fm.createDirectory(at: staging, withIntermediateDirectories: true)
            for part in entries(root) where ["romfs", "exefs", "sdmc"].contains(part.lowercased()) {
                if onePart && root.appendingPathComponent(part).standardizedFileURL != boundary.standardizedFileURL { continue }
                try copyPayload(root.appendingPathComponent(part), staging.appendingPathComponent(part.lowercased()))
            }
            for companion in sdCompanions(root, within: boundary) {
                let sd = staging.appendingPathComponent("sdmc")
                try fm.createDirectory(at: sd, withIntermediateDirectories: true)
                try copyPayload(companion, sd.appendingPathComponent(companion.lastPathComponent))
            }
            guard hasRomfs(staging) || hasCode(staging) || hasSdmc(staging) else {
                throw NSError(domain: "Inkypaint.Mods", code: 1, userInfo: [NSLocalizedDescriptionKey: "No usable romfs or exefs was copied from \(root.path)."])
            }
            try fm.moveItem(at: staging, to: to)
        } catch {
            try? fm.removeItem(at: staging)
            throw error
        }
    }
}

/// The Mods page: the game's mods folder, each mod on or off (mods_off= in the game's .ini), and adding one from a
/// folder or an archive. GRID0+'s own patches are left to the Online page.
@Observable
@MainActor
final class ModsModel {
    struct Mod: Identifiable, Equatable {
        let name: String
        let on: Bool
        let romfs: Bool
        let code: Bool
        let sdmc: Bool
        let issue: String?
        var id: String { name }
    }

    private(set) var mods: [Mod] = []
    private(set) var needsRecompile = false
    private(set) var busy = false
    var error: String?
    var notice: String?

    private static let section = "Game"

    func folder(_ paths: GamePaths) -> URL { Grid0Patches.modsFolder(paths) }

    func refresh(_ paths: GamePaths, _ settings: GameSettings) {
        let off = settings.file.value("mods_off") ?? "", dir = folder(paths)
        mods = ModFiles.list(dir).filter { !$0.hasPrefix("grid0plus-") }.map { name in
            let mod = dir.appendingPathComponent(name)
            let issue = ModFiles.normalizeInstalled(mod)
            return Mod(name: name, on: !ModFiles.isOff(off, name), romfs: ModFiles.hasRomfs(mod), code: ModFiles.hasCode(mod), sdmc: ModFiles.hasSdmc(mod), issue: issue)
        }
        // the code the game was recompiled with, against the code mods played now (GRID0+'s included)
        needsRecompile = paths.isPrepared
            && ModFiles.stamp(dir, off: off) != ModFiles.recorded(paths.gameDir.appendingPathComponent("\(Game.titleID).abi"))
    }

    func setOn(_ name: String, _ on: Bool, _ paths: GamePaths, _ settings: GameSettings) {
        settings.setRaw("mods_off", ModFiles.setOff(settings.file.value("mods_off") ?? "", name, !on), section: Self.section)
        refresh(paths, settings)
    }

    func remove(_ name: String, _ paths: GamePaths, _ settings: GameSettings) {
        guard !name.isEmpty, !name.contains("/"), !name.hasPrefix(".") else { return }
        do {
            try FileManager.default.removeItem(at: folder(paths).appendingPathComponent(name))
            notice = "Removed \(name)."
        } catch {
            self.error = "Couldn't remove all of \(name). Is the game running?"
        }
        settings.setRaw("mods_off", ModFiles.setOff(settings.file.value("mods_off") ?? "", name, false), section: Self.section)
        refresh(paths, settings)
    }

    /// A folder or an archive (.zip, .7z, .rar: the system's bsdtar reads them)
    func add(_ source: URL, _ paths: GamePaths, _ settings: GameSettings) async {
        guard !busy else { return }
        busy = true
        error = nil
        notice = nil
        let mods = folder(paths)
        let result = await Task.detached { () -> (added: [String], error: String?) in
            let fm = FileManager.default
            var isDir: ObjCBool = false
            guard fm.fileExists(atPath: source.path, isDirectory: &isDir) else { return ([], "\(source.lastPathComponent) isn't there any more.") }
            try? fm.createDirectory(at: mods, withIntermediateDirectories: true)
            var base = source, unpacked: URL?
            let fallback = isDir.boolValue ? source.lastPathComponent : source.deletingPathExtension().lastPathComponent
            defer { if let unpacked { try? fm.removeItem(at: unpacked) } }
            if !isDir.boolValue {
                let dir = mods.deletingLastPathComponent().appendingPathComponent(".mod-unpacking-\(UUID().uuidString)")
                unpacked = dir
                try? fm.createDirectory(at: dir, withIntermediateDirectories: true)
                let tar = Process()
                tar.executableURL = URL(fileURLWithPath: "/usr/bin/tar")
                tar.arguments = ["-xf", source.path, "-C", dir.path]
                do {
                    try tar.run()
                    tar.waitUntilExit()
                } catch {}
                guard tar.terminationReason == .exit, tar.terminationStatus == 0 else {
                    return ([], "Couldn't unpack \(source.lastPathComponent). Unpack it yourself and add the folder.")
                }
                base = dir
            }
            let roots = ModFiles.roots(base)
            if roots.isEmpty {
                return ([], "\(fallback) has no romfs or exefs folder, so it isn't a mod inkypaint can use. A mod keeps its "
                            + "files in romfs/ (and code patches in exefs/).")
            }
            var added: [String] = []
            for root in roots {
                let name = ModFiles.freeName(mods, wanted: root == base ? fallback : root.lastPathComponent, fallback: fallback)
                do {
                    try ModFiles.install(root, to: mods.appendingPathComponent(name), packageBase: base)
                } catch {
                    return (added, "Couldn't copy the mod: \(error.localizedDescription)")
                }
                added.append(name)
            }
            return (added, nil)
        }.value
        busy = false
        error = result.error
        notice = result.added.isEmpty ? nil
            : result.added.count == 1 ? "Added \(result.added[0])."
            : "Added \(result.added.count) mods: \(result.added.joined(separator: ", "))."
        refresh(paths, settings)
    }
}
