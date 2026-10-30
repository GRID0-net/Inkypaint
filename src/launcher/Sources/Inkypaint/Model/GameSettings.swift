// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import Foundation
import Observation

/// One of the runtime's settings the launcher shows (src/nx/runtime/settings.txt holds them all: the key, its section
/// in the .ini, its values and whether it applies only when the game next starts).
struct SettingDef: Identifiable {
    enum Kind {
        case controllerMap
        case toggle
        case choice([(value: String, label: String)])
        case networkAdapter // a choice of the Mac's adapters, listed when the settings show
        case key(String)    // a control's keys (key_<id>, kbm.c): "" for the game's default, these keys
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
    static let controllerControls: [(key: String, title: String, fallback: String)] = [
        ("south", "Bottom (A / Cross)", "B"),
        ("east", "Right (B / Circle)", "A"),
        ("west", "Left (X / Square)", "Y"),
        ("north", "Top (Y / Triangle)", "X"),
        ("lb", "Left bumper", "L"),
        ("rb", "Right bumper", "R"),
        ("lt", "Left trigger", "ZL"),
        ("rt", "Right trigger", "ZR"),
        ("ls", "Left stick click", "L3"),
        ("rs", "Right stick click", "R3"),
        ("back", "Back / Share", "Minus"),
        ("start", "Start / Options", "Plus"),
        ("up", "D-pad up", "Up"),
        ("down", "D-pad down", "Down"),
        ("left", "D-pad left", "Left"),
        ("right", "D-pad right", "Right")
    ]
    static let controllerButtons = "A B X Y L R ZL ZR Plus Minus L3 R3 Up Down Left Right None".components(separatedBy: " ")

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
                       detail: "Scale the 3D scene, keeping the interface at its normal resolution. 1× follows the game.",
                       kind: .choice([("0.25", "0.25×"), ("0.5", "0.5×"), ("0.75", "0.75×"), ("1", "1× (console)"),
                                      ("1.5", "1.5×"), ("2", "2×"), ("3", "3×"), ("4", "4×")]),
                       defaultValue: "1", nextLaunch: true),
            SettingDef(key: "fps_limit", section: "Performance", title: "Frame rate",
                       detail: "F4 turns the limit off and on while you play.",
                       kind: .choice([("game", "As the game asks (60)"), ("30", "30 FPS"), ("100", "100 FPS"), ("120", "120 FPS"),
                                      ("0", "Unlimited")]),
                       defaultValue: "game"),
            SettingDef(key: "smooth_motion", section: "Performance", title: "Smooth motion (experimental)",
                       detail: "Experimental interpolation with 60 Hz game logic. Uses the frame rate chosen above when it exceeds "
                           + "60 FPS; otherwise targets 120. Requires GPU headroom.",
                       kind: .toggle, defaultValue: "0", nextLaunch: true),
            SettingDef(key: "vsync", section: "Graphics", title: "VSync", detail: "Waits for the display: no tearing.",
                       kind: .toggle, defaultValue: "1"),
            SettingDef(key: "anisotropy", section: "Graphics", title: "Texture filtering",
                       detail: "Sharper textures at a glancing angle.",
                       kind: .choice([("0", "As the game asks"), ("1", "Off"), ("2", "2×"), ("4", "4×"), ("8", "8×"), ("16", "16×")]),
                       defaultValue: "0", nextLaunch: true),
            SettingDef(key: "shadows", section: "Graphics", title: "Shadows",
                       detail: "Disable shadow maps and shadow post-processing in the engine.", kind: .toggle, defaultValue: "1", nextLaunch: true),
            SettingDef(key: "bloom", section: "Graphics", title: "Bloom", detail: "Bright lights glow.", kind: .toggle, defaultValue: "1", nextLaunch: true),
            SettingDef(key: "depth_of_field", section: "Graphics", title: "Depth of field", detail: "The game's focus blur.", kind: .toggle, defaultValue: "1", nextLaunch: true),
            SettingDef(key: "vignette", section: "Graphics", title: "Vignette", detail: "The game's vignette pass.", kind: .toggle, defaultValue: "1", nextLaunch: true),
            SettingDef(key: "fog", section: "Graphics", title: "Fog", detail: "Depth and height fog.", kind: .toggle, defaultValue: "1", nextLaunch: true),
            SettingDef(key: "dynamic_resolution", section: "Graphics", title: "Dynamic resolution",
                       detail: "Adjust scene quality to completed GPU work. Resolution is the maximum scale.", kind: .toggle, defaultValue: "0", nextLaunch: true),
            SettingDef(key: "dynamic_resolution_min", section: "Graphics", title: "Minimum render scale",
                       detail: "The lower limit for dynamic resolution.",
                       kind: .choice([("0.25", "0.25×"), ("0.5", "0.5×"), ("0.625", "0.625×"), ("0.75", "0.75×"), ("1", "1×")]), defaultValue: "0.5", nextLaunch: true),
            SettingDef(key: "dynamic_resolution_fps", section: "Graphics", title: "Dynamic resolution target",
                       detail: "GPU frame budget. Does not change the game's speed or frame-rate limit.",
                       kind: .choice([("30", "30 FPS"), ("60", "60 FPS"), ("90", "90 FPS"), ("120", "120 FPS")]), defaultValue: "60", nextLaunch: true),
            SettingDef(key: "texture_lod_bias", section: "Graphics", title: "Texture detail",
                       detail: "Coarser mipmaps reduce sampling detail. Texture allocations stay the same.",
                       kind: .choice([("0", "Game"), ("1", "Reduced"), ("2", "Low"), ("3", "Lowest")]), defaultValue: "0", nextLaunch: true),
            SettingDef(key: "presentation_filter", section: "Graphics", title: "Window scaling filter",
                       detail: "Resizing the final image. The game upscales the scene separately.",
                       kind: .choice([("1", "Linear"), ("0", "Nearest")]), defaultValue: "1"),
        ]),
        SettingGroup(title: "Controls", symbol: "gamecontroller", settings: [
            SettingDef(key: "keyboard_mouse_enabled", section: "Keyboard and mouse", title: "Keyboard and mouse input",
                       detail: "Controller activity takes priority. Keyboard and mouse resume after the controller is idle.", kind: .toggle, defaultValue: "1"),
            SettingDef(key: "gamepad_menu_layout", section: "Controllers", title: "Xbox/PlayStation layout in menus",
                       detail: "Bottom button confirms in menus and activates visible A-button interaction prompts. Gameplay restores its own layout when the prompt disappears. Prompts keep Nintendo labels.", kind: .toggle, defaultValue: "0"),
        ]),
        SettingGroup(title: "Controller input", symbol: "gamecontroller", settings: [
            SettingDef(key: "gamepad_deadzone", section: "Controllers", title: "Stick dead zone", detail: "Ignore small stick movements around the center.",
                       kind: .choice([("0", "0%"), ("5", "5%"), ("10", "10%"), ("15", "15%"), ("20", "20%"), ("25", "25%"), ("30", "30%"), ("40", "40%")]), defaultValue: "20"),
            SettingDef(key: "gamepad_rumble", section: "Controllers", title: "Rumble",
                       detail: "How strongly your controller vibrates when the game asks. Off turns it off.",
                       kind: .choice([("0", "Off"), ("40", "Light"), ("70", "Medium"), ("100", "Full")]),
                       defaultValue: "100"),
            SettingDef(key: "gamepad_map", section: "Controllers", title: "Controller buttons",
                       detail: "Gameplay mappings. The optional menu layout swaps A/B and X/Y in menus.", kind: .controllerMap, defaultValue: ""),
        ]),
        SettingGroup(title: "Keyboard and mouse", symbol: "keyboard", settings: [
            SettingDef(key: "mouse_aim", section: "Keyboard and mouse", title: "Mouse aim",
                       detail: "Click in the game's window and the mouse aims the way motion controls do (turn the "
                           + "game's motion controls on). Left Option lets the mouse go, and the game's menu does too.",
                       kind: .toggle, defaultValue: "1"),
            SettingDef(key: "native_mouse_camera", section: "Keyboard and mouse", title: "Native mouse camera",
                       detail: "The mouse turns the camera directly, like a PC game, with no need for the game's "
                           + "motion controls. When it is off, the original controller camera runs.",
                       kind: .toggle, defaultValue: "0"),
            SettingDef(key: "mouse_sensitivity", section: "Keyboard and mouse", title: "Mouse sensitivity",
                       detail: "How far the mouse turns the aim. The game's own motion sensitivity applies on top.",
                       kind: .choice([("25", "25%"), ("50", "50%"), ("75", "75%"), ("100", "100%"), ("125", "125%"),
                                      ("150", "150%"), ("200", "200%"), ("300", "300%"), ("400", "400%")]),
                       defaultValue: "100"),
            SettingDef(key: "mouse_sensitivity_y", section: "Keyboard and mouse", title: "Vertical sensitivity",
                       detail: "The up and down aim, compared to left and right.",
                       kind: .choice([("50", "50%"), ("75", "75%"), ("100", "Same"), ("125", "125%"), ("150", "150%")]),
                       defaultValue: "100"),
            SettingDef(key: "mouse_invert_y", section: "Keyboard and mouse", title: "Invert vertical aim", detail: "",
                       kind: .toggle, defaultValue: "0"),
        ] + SettingsCatalog.keys.map { id, title, fallback in
            SettingDef(key: "key_\(id)", section: "Keyboard and mouse", title: title, detail: "", kind: .key(fallback),
                       defaultValue: "")
        }),
        SettingGroup(title: "Keys in menus", symbol: "keyboard", settings: [
            SettingDef(key: "mouse_in_menus", section: "Keyboard and mouse", title: "Mouse in menus",
                       detail: "The game's menus let the mouse go: point at a button and the menu's cursor goes there, "
                           + "click to pick it. The keys below are the menus' own while one is up.",
                       kind: .toggle, defaultValue: "1"),
        ] + SettingsCatalog.menuKeys.map { id, title, fallback in
            SettingDef(key: "menu_key_\(id)", section: "Keyboard and mouse", title: title, detail: "",
                       kind: .key(fallback), defaultValue: "")
        }),
        SettingGroup(title: "LAN play", symbol: "network", settings: [
            SettingDef(key: "network", section: "Network", title: "Network",
                       detail: "Lets the game use your connection. LAN play and online play need it.",
                       kind: .toggle, defaultValue: "0"),
            SettingDef(key: "lan_adapter", section: "Network", title: "LAN adapter",
                       detail: "Where the game looks for LAN and local-play lobbies. Pick ZeroTier's adapter to play "
                           + "with friends on your ZeroTier network as if you were on one LAN.",
                       kind: .networkAdapter, defaultValue: ""),
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

    /// The controls the keyboard and mouse press (settings.txt's list kbm), and the game's default keys of each (kbm.c)
    static let keys: [(String, String, String)] = [
        ("ls_up", "Move forward", "W"), ("ls_down", "Move back", "S"), ("ls_left", "Move left", "A"),
        ("ls_right", "Move right", "D"), ("zr", "Shoot (ZR)", "Mouse Left"), ("r", "Sub weapon (R)", "Mouse Right"),
        ("zl", "Swim (ZL)", "Left Shift"), ("b", "Jump (B)", "Space, Backspace"),
        ("r3", "Special weapon (R stick)", "E"), ("x", "Map (X)", "Tab"), ("y", "Camera behind you (Y)", "R"),
        ("a", "A", "F, Return"), ("l", "L", "Q"), ("l3", "L stick click", "C"), ("up", "This way! (D-pad up)", "Up"),
        ("down", "Booyah! (D-pad down)", "Down"), ("left", "D-pad left", "Left"), ("right", "D-pad right", "Right"),
        ("plus", "Plus", "P"), ("minus", "Minus", "M"), ("rs_up", "Camera up", "I"), ("rs_down", "Camera down", "K"),
        ("rs_left", "Camera left", "J"), ("rs_right", "Camera right", "L"), ("cursor", "Free the mouse", "Left Alt"),
    ]

    /// The same controls' keys in the game's menus (menu_key_<id>, kbm.c's menu_defaults)
    static let menuKeys: [(String, String, String)] = [
        ("a", "Pick (A)", "Return, Space, F"), ("b", "Back (B)", "Backspace, Mouse Right"), ("l", "Tab left (L)", "Q"),
        ("r", "Tab right (R)", "E"), ("x", "X", "X, Tab"), ("y", "Y", "Y"), ("zl", "ZL", "Z"), ("zr", "ZR", "C"),
        ("up", "Up", "Up, W"), ("down", "Down", "Down, S"), ("left", "Left", "Left, A"), ("right", "Right", "Right, D"),
        ("plus", "Plus", "P"), ("minus", "Minus", "M"), ("l3", "L stick click", "Left Shift"),
        ("r3", "R stick click", "Left Ctrl"), ("rs_up", "Right stick up", "I"), ("rs_down", "Right stick down", "K"),
        ("rs_left", "Right stick left", "J"), ("rs_right", "Right stick right", "L"),
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

    var smoothMotionEnabled: Bool { (file.value("smooth_motion") ?? "0") != "0" }

    func value(_ def: SettingDef) -> String { file.value(def.key) ?? def.defaultValue }

    func set(_ def: SettingDef, _ value: String) {
        guard self.value(def) != value else { return }
        file.set(def.key, value, section: def.section)
        if def.key == "smooth_motion" && value == "0" {
            file.set("fps_limit", "game", section: "Performance")
        }
        do {
            try file.save(to: url)
            error = nil
        } catch {
            self.error = "Couldn't save the settings: \(error.localizedDescription)"
        }
    }

    /// A key the catalog doesn't show (the online settings), in section when the file doesn't have it yet
    func setRaw(_ key: String, _ value: String, section: String) {
        guard file.value(key) != value else { return }
        file.set(key, value, section: section)
        do {
            try file.save(to: url)
            error = nil
        } catch {
            self.error = "Couldn't save the settings: \(error.localizedDescription)"
        }
    }

    func toggle(_ def: SettingDef) -> Bool { value(def) != "0" }

    func applyGraphicsPreset(_ name: String) {
        guard ["Performance", "Balanced", "Quality"].contains(name) else { return }
        let fast = name == "Performance", high = name == "Quality"
        let values = [
            "resolution": fast ? "0.75" : high ? "1.5" : "1",
            "dynamic_resolution": high ? "0" : "1", "dynamic_resolution_min": fast ? "0.5" : "0.625",
            "dynamic_resolution_fps": "60", "shadows": fast ? "0" : "1", "bloom": fast ? "0" : "1",
            "depth_of_field": high ? "1" : "0", "vignette": fast ? "0" : "1", "fog": "1",
            "anisotropy": fast ? "1" : high ? "16" : "4", "texture_lod_bias": fast ? "1" : "0",
            "presentation_filter": "1", "smooth_motion": "0", "fps_limit": "game", "vsync": "1",
        ]
        for def in SettingsCatalog.all {
            if let value = values[def.key] { file.set(def.key, value, section: def.section) }
        }
        do { try file.save(to: url); error = nil }
        catch { self.error = "Couldn't save the settings: \(error.localizedDescription)" }
    }

    func controllerValue(_ control: String) -> String {
        let def = SettingsCatalog.all.first { $0.key == "gamepad_map" }!
        for token in value(def).split(whereSeparator: { $0.isWhitespace }) {
            let pair = token.split(separator: ":", maxSplits: 1)
            if pair.count == 2 && pair[0].lowercased() == control { return String(pair[1]) }
        }
        return SettingsCatalog.controllerControls.first { $0.key == control }?.fallback ?? "None"
    }
    func setControllerMapping(_ control: String, _ target: String) {
        guard SettingsCatalog.controllerControls.contains(where: { $0.key == control }),
              SettingsCatalog.controllerButtons.contains(target) else { return }
        let def = SettingsCatalog.all.first { $0.key == "gamepad_map" }!
        var tokens = value(def).split(whereSeparator: { $0.isWhitespace }).map(String.init)
        tokens.removeAll { $0.split(separator: ":").first?.lowercased() == control }
        tokens.append(control + ":" + target)
        set(def, tokens.joined(separator: " "))
    }

    func resetAll() {
        for def in SettingsCatalog.all { set(def, def.defaultValue) }
    }

    /// The label a choice shows, for the Play screen's summary.
    func label(_ key: String) -> String {
        guard let def = SettingsCatalog.all.first(where: { $0.key == key }) else { return "" }
        let v = value(def)
        switch def.kind {
        case .controllerMap: return "Custom buttons"
        case .toggle: return v == "0" ? "Off" : "On"
        case .choice(let options): return options.first { $0.value == v }?.label ?? v
        case .networkAdapter: return NetworkAdapter.choices().first { $0.value == v }?.label ?? v
        case .key(let fallback): return v == "None" ? "No key" : KeyNames.shown(v.isEmpty ? fallback : v)
        }
    }
}
