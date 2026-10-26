// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import CryptoKit
import Foundation
import Observation

/// GRID0+'s patches to the game's code (the certificate pinning it skips, the small-match fix...): IPS files for the
/// 11.3.0 executable, from the newest patches-<serial> release of GRID0-net/GRID0plus-cfw, the release the console
/// Toolbox and the GRID0 emulators install from. inkypaint compiles the game ahead of time, so a patch is a mod
/// (mods/grid0plus-<category>/exefs/<build ID>.ips in the game's data folder), and the game is recompiled with it.
///
/// Each file is checked against the release manifest's SHA-256 before it is written. The grid0plus- mods are this
/// class's alone: one the manifest no longer lists is removed. The files are downloaded, never kept in the app.
@Observable
@MainActor
final class Grid0Patches {
    enum Status: Equatable {
        case unknown, checking
        case current(String)          // the release's tag
        case failed(String)
    }

    private(set) var status: Status = .unknown
    /// Files were added, replaced or removed since the last recompile (kept across launches)
    private(set) var changed = UserDefaults.standard.bool(forKey: "grid0PatchesChanged")
    private func setChanged(_ on: Bool) {
        changed = on
        UserDefaults.standard.set(on, forKey: "grid0PatchesChanged")
    }

    private static let releases = URL(string: "https://api.github.com/repos/GRID0-net/GRID0plus-cfw/releases?per_page=30")!
    private static let downloads = "https://github.com/GRID0-net/GRID0plus-cfw/releases/download/"
    private static let prefix = "grid0plus-"

    struct Entry {
        let category: String
        let sha256: String
        let asset: String
    }

    static func modsFolder(_ paths: GamePaths) -> URL {
        paths.gameDir.appendingPathComponent("\(Game.titleID)_data/mods", isDirectory: true)
    }
    static func file(_ paths: GamePaths, _ category: String) -> URL {
        modsFolder(paths).appendingPathComponent("\(prefix)\(category)/exefs/\(Grid0.gameBuild).ips")
    }
    /// Whether any patch is installed
    static func installed(_ paths: GamePaths) -> Bool {
        let mods = (try? FileManager.default.contentsOfDirectory(atPath: modsFolder(paths).path)) ?? []
        return mods.contains { $0.hasPrefix(prefix) }
    }
    /// Whether the game was recompiled with mods: the runtime's stamp says so ("<abi> mods <hash>")
    static func compiledIn(_ paths: GamePaths) -> Bool {
        let stamp = paths.gameDir.appendingPathComponent("\(Game.titleID).abi")
        return ((try? String(contentsOf: stamp, encoding: .utf8)) ?? "").contains(" mods ")
    }
    /// The game must be recompiled before the patches count
    func needsRecompile(_ paths: GamePaths) -> Bool {
        Self.installed(paths) && (changed || !Self.compiledIn(paths))
    }
    func recompiled() { setChanged(false) }

    /// Brings the patches up to date with the newest release
    func update(_ paths: GamePaths) async {
        guard status != .checking else { return }
        status = .checking
        do {
            let tag = try await newestTag()
            let entries = try await manifest(tag)
            guard !entries.isEmpty else { throw Grid0Error.refused("The newest GRID0+ release has no patches for 11.3.0.") }
            let fm = FileManager.default
            var downloaded: [(Entry, Data)] = []
            for e in entries where (try? Data(contentsOf: Self.file(paths, e.category))).map(Self.sha256) != e.sha256 {
                let (data, response) = try await URLSession.shared.data(from: URL(string: Self.downloads + tag + "/" + e.asset)!)
                guard (response as? HTTPURLResponse)?.statusCode == 200, Self.sha256(data) == e.sha256 else {
                    throw Grid0Error.refused("\(e.category) doesn't match the release's manifest. Nothing was changed.")
                }
                downloaded.append((e, data))
            }
            // nothing is written until every file has checked out
            for (e, data) in downloaded {
                let url = Self.file(paths, e.category)
                try fm.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
                try data.write(to: url, options: .atomic)
            }
            let wanted = Set(entries.map { Self.prefix + $0.category })
            var removed = false
            for mod in (try? fm.contentsOfDirectory(atPath: Self.modsFolder(paths).path)) ?? []
            where mod.hasPrefix(Self.prefix) && !wanted.contains(mod) {
                try? fm.removeItem(at: Self.modsFolder(paths).appendingPathComponent(mod))
                removed = true
            }
            if !downloaded.isEmpty || removed { setChanged(true) }
            status = .current(tag)
        } catch {
            status = .failed(error.localizedDescription)
        }
    }

    private static func sha256(_ data: Data) -> String {
        SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
    }

    private func get(_ url: URL) async throws -> Data {
        var request = URLRequest(url: url, timeoutInterval: 30)
        request.setValue("inkypaint", forHTTPHeaderField: "User-Agent")
        let (data, response) = try await URLSession.shared.data(for: request)
        guard (response as? HTTPURLResponse)?.statusCode == 200 else {
            throw Grid0Error.unreachable("GitHub answered \((response as? HTTPURLResponse)?.statusCode ?? 0)")
        }
        return data
    }

    /// The highest patches-<serial> release that isn't a draft
    private func newestTag() async throws -> String {
        let list = (try JSONSerialization.jsonObject(with: try await get(Self.releases))) as? [[String: Any]] ?? []
        let tags = list.compactMap { r -> (Int, String)? in
            guard r["draft"] as? Bool != true, let tag = r["tag_name"] as? String, tag.hasPrefix("patches-"),
                  let serial = Int(tag.dropFirst(8)) else { return nil }
            return (serial, tag)
        }
        guard let newest = tags.max(by: { $0.0 < $1.0 }) else { throw Grid0Error.refused("No GRID0+ patch release was found.") }
        return newest.1
    }

    /// The manifest's patches for emulators and this build: exefs_patches/<category>/<build ID>.ips, the asset named
    /// by its hash. A malformed entry rejects the whole manifest
    private func manifest(_ tag: String) async throws -> [Entry] {
        let data = try await get(URL(string: Self.downloads + tag + "/manifest.json")!)
        let patches = ((try JSONSerialization.jsonObject(with: data)) as? [String: Any])?["patches"] as? [[String: Any]] ?? []
        let safe = #/^exefs_patches/([A-Za-z0-9_-][A-Za-z0-9_.-]*)/([A-Fa-f0-9]+)\.ips$/#
        var out: [Entry] = []
        for p in patches where (p["targets"] as? [String])?.contains("emulator") == true {
            guard let dest = p["dest"] as? String, let sha = p["sha256"] as? String, let asset = p["asset"] as? String,
                  let match = dest.wholeMatch(of: safe), sha.wholeMatch(of: #/[a-f0-9]{64}/#) != nil, asset == sha + ".ips"
            else { throw Grid0Error.refused("The GRID0+ manifest has an entry inkypaint can't trust.") }
            if String(match.2).uppercased() == Grid0.gameBuild { out.append(Entry(category: String(match.1), sha256: sha, asset: asset)) }
        }
        return out
    }
}
