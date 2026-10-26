// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import Foundation

/// The game's settings file as the runtime writes it: `[Section]` headers, `key=value` lines and `;` comments.
/// Only the lines of the keys set change; comments, order and every other key stay as they are.
struct IniFile {
    private(set) var lines: [String]

    init(text: String) { lines = text.components(separatedBy: "\n") }

    static func load(_ url: URL) -> IniFile {
        IniFile(text: (try? String(contentsOf: url, encoding: .utf8)) ?? "")
    }

    func save(to url: URL) throws {
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        try lines.joined(separator: "\n").write(to: url, atomically: true, encoding: .utf8)
    }

    private func index(of key: String) -> Int? {
        lines.firstIndex { line in
            let trimmed = line.trimmingCharacters(in: .whitespaces)
            guard !trimmed.hasPrefix(";"), let eq = trimmed.firstIndex(of: "=") else { return false }
            return trimmed[..<eq].trimmingCharacters(in: .whitespaces) == key
        }
    }

    func value(_ key: String) -> String? {
        guard let i = index(of: key), let eq = lines[i].firstIndex(of: "=") else { return nil }
        return lines[i][lines[i].index(after: eq)...].trimmingCharacters(in: .whitespaces)
    }

    /// Sets key, in section when the file doesn't have it yet (the section is added at the end if it is missing).
    mutating func set(_ key: String, _ value: String, section: String) {
        if let i = index(of: key) {
            lines[i] = "\(key)=\(value)"
            return
        }
        let header = "[\(section)]"
        if let h = lines.firstIndex(where: { $0.trimmingCharacters(in: .whitespaces) == header }) {
            var at = h + 1 // after the section's last key, before the next section
            var i = h + 1
            while i < lines.count, !lines[i].trimmingCharacters(in: .whitespaces).hasPrefix("[") {
                if !lines[i].trimmingCharacters(in: .whitespaces).isEmpty { at = i + 1 }
                i += 1
            }
            lines.insert("\(key)=\(value)", at: at)
        } else {
            if let last = lines.last, !last.isEmpty { lines.append("") }
            lines.append(header)
            lines.append("\(key)=\(value)")
        }
    }
}
