// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import Foundation
import Observation

/// One of the runtime's settings the launcher shows (src/nx/runtime/settings.txt holds them all: the key, its section
/// in the .ini, its values and whether it applies only when the game next starts).
struct SettingDef: Identifiable {
    enum Kind {
        case toggle
        case choice([(value: String, label: String)])
    }
    let key: String
    let section: String
    let title: String
    let detail: String
    let kind: Kind
    let defaultValue: String
    var nextLaunch = false
    var id: String { key }
}

struct SettingGroup: Identifiable {
    let title: String
    let symbol: String
    let settings: [SettingDef]
    var id: String { title }
}

enum SettingsCatalog {
    static let languages = [
        "Japanese", "English (US)", "French", "German", "Italian", "Spanish", "Chinese", "Korean", "Dutch",
        "Portuguese", "Russian", "Taiwanese", "English (UK)", "French (Canada)", "Spanish (Latin America)",
        "Chinese (simplified)", "Chinese (traditional)", "Portuguese (Brazil)",
    ]

    static let groups: [SettingGroup] = [
        SettingGroup(title: "Display", symbol: "display", settings: [
            SettingDef(key: "fullscreen", section: "Display", title: "Start in full screen",
                       detail: "F11 or Option-Return switches while you play.", kind: .toggle, defaultValue: "0",
                       nextLaunch: true),
            SettingDef(key: "docked", section: "Display", title: "TV mode",
                       detail: "The console in its dock. Off plays as handheld, at 1280×720.", kind: .toggle,
                       defaultValue: "1", nextLaunch: true),
            SettingDef(key: "adapt_aspect", section: "Graphics", title: "Fill wide windows",
                       detail: "Widens the world to the window, the interface stays 16:9.", kind: .toggle,
                       defaultValue: "1", nextLaunch: true),
            SettingDef(key: "language", section: "Display", title: "Game language", detail: "",
                       kind: .choice(languages.enumerated().map { (String($0.offset), $0.element) }),
                       defaultValue: "1", nextLaunch: true),
        ]),
        SettingGroup(title: "Graphics", symbol: "paintbrush.pointed", settings: [
            SettingDef(key: "resolution", section: "Graphics", title: "Resolution",
                       detail: "The scale of the 3D scene. 1× is the console's 1080p.",
                       kind: .choice([("0.5", "0.5× (fastest)"), ("0.75", "0.75×"), ("1", "1× (console)"),
                                      ("1.5", "1.5×"), ("2", "2× (sharpest)")]),
                       defaultValue: "1", nextLaunch: true),
            SettingDef(key: "fps_limit", section: "Performance", title: "Frame rate",
                       detail: "F4 turns the limit off and on while you play.",
                       kind: .choice([("game", "As the game asks (60)"), ("30", "30 FPS"), ("120", "120 FPS"),
                                      ("0", "Unlimited")]),
                       defaultValue: "game"),
            SettingDef(key: "vsync", section: "Graphics", title: "VSync", detail: "Waits for the display: no tearing.",
                       kind: .toggle, defaultValue: "1"),
            SettingDef(key: "anisotropy", section: "Graphics", title: "Texture filtering",
                       detail: "Sharper textures at a glancing angle.",
                       kind: .choice([("0", "As the game asks"), ("4", "4×"), ("8", "8×"), ("16", "16×")]),
                       defaultValue: "0", nextLaunch: true),
            SettingDef(key: "shadows", section: "Graphics", title: "Shadows", detail: "", kind: .toggle,
                       defaultValue: "1"),
        ]),
        SettingGroup(title: "Shaders", symbol: "sparkles", settings: [
            SettingDef(key: "async_pipelines", section: "Performance", title: "Compile shaders in the background",
                       detail: "No pause when something new appears: it shows up a few frames late instead.",
                       kind: .toggle, defaultValue: "1"),
            SettingDef(key: "prepare_shaders", section: "Performance", title: "Shaders of earlier runs",
                       detail: "Made before the game starts for no stutter, or while it plays to start sooner.",
                       kind: .choice([("2", "Before starting"), ("1", "While playing"), ("0", "Off")]),
                       defaultValue: "2", nextLaunch: true),
            SettingDef(key: "precise_float", section: "Performance", title: "Shader math",
                       detail: "Fast lets Metal optimize the game's shaders. Exact rounds as the Switch does.",
                       kind: .choice([("2", "Fast (recommended)"), ("1", "Exact"), ("0", "Fast everywhere")]),
                       defaultValue: "2"),
        ]),
    ]

    static var all: [SettingDef] { groups.flatMap(\.settings) }
}

/// The game's .ini, read and written as settings change. The runtime makes the file at the game's first start; until
/// then the launcher writes the keys it sets into a new one, which the runtime fills in.
@Observable
@MainActor
final class GameSettings {
    private(set) var file: IniFile
    let url: URL
    private(set) var error: String?

    init(url: URL) {
        self.url = url
        file = IniFile.load(url)
    }

    var exists: Bool { FileManager.default.fileExists(atPath: url.path) }

    func reload() { file = IniFile.load(url) }

    func value(_ def: SettingDef) -> String { file.value(def.key) ?? def.defaultValue }

    func set(_ def: SettingDef, _ value: String) {
        guard self.value(def) != value else { return }
        file.set(def.key, value, section: def.section)
        do {
            try file.save(to: url)
            error = nil
        } catch {
            self.error = "Couldn't save the settings: \(error.localizedDescription)"
        }
    }

    func toggle(_ def: SettingDef) -> Bool { value(def) != "0" }

    func resetAll() {
        for def in SettingsCatalog.all { set(def, def.defaultValue) }
    }

    /// The label a choice shows, for the Play screen's summary.
    func label(_ key: String) -> String {
        guard let def = SettingsCatalog.all.first(where: { $0.key == key }) else { return "" }
        let v = value(def)
        switch def.kind {
        case .toggle: return v == "0" ? "Off" : "On"
        case .choice(let options): return options.first { $0.value == v }?.label ?? v
        }
    }
}
