// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import Foundation
// UI key-label formatting is outside this settings-file test.
enum KeyNames { static func shown(_ value: String) -> String { value } }
@main
struct GraphicsPresetTest {
    @MainActor static func main() throws {
        let folder = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: folder) }
        let url = folder.appendingPathComponent("game.ini")
        let settings = GameSettings(url: url)
        settings.setRaw("grid0_user", "fixture-user", section: "Network")
        settings.setRaw("key_a", "K", section: "Keyboard and mouse")
        func value(_ key: String) -> String { settings.file.value(key) ?? "" }
        settings.applyGraphicsPreset("Performance")
        precondition(settings.error == nil && value("resolution") == "0.75")
        precondition(value("dynamic_resolution") == "1" && value("shadows") == "0")
        precondition(value("texture_lod_bias") == "1" && value("anisotropy") == "1")
        precondition(value("fps_limit") == "game" && value("smooth_motion") == "0")
        settings.applyGraphicsPreset("Balanced")
        precondition(value("resolution") == "1" && value("dynamic_resolution_min") == "0.625")
        precondition(value("shadows") == "1" && value("depth_of_field") == "0")
        settings.applyGraphicsPreset("Quality")
        precondition(value("resolution") == "1.5" && value("dynamic_resolution") == "0")
        precondition(value("anisotropy") == "16" && value("depth_of_field") == "1")
        let before = try Data(contentsOf: url)
        settings.applyGraphicsPreset("unknown")
        let after = try Data(contentsOf: url)
        precondition(after == before)
        let reopened = GameSettings(url: url)
        precondition(reopened.file.value("resolution") == "1.5")
        precondition(reopened.file.value("grid0_user") == "fixture-user" && reopened.file.value("key_a") == "K")
        reopened.setControllerMapping("south", "A")
        reopened.setControllerMapping("lt", "ZR")
        reopened.setControllerMapping("south", "B")
        precondition(reopened.controllerValue("south") == "B" && reopened.controllerValue("lt") == "ZR")
        precondition(GameSettings(url: url).controllerValue("lt") == "ZR")
        for key in ["bloom", "shadows", "depth_of_field"] {
            precondition(SettingsCatalog.all.first { $0.key == key }?.nextLaunch == true)
        }
        let smooth = SettingsCatalog.all.first { $0.key == "smooth_motion" }!
        let fps = SettingsCatalog.all.first { $0.key == "fps_limit" }!
        precondition(!settings.smoothMotionEnabled)
        settings.set(smooth, "1")
        settings.set(fps, "100")
        precondition(settings.smoothMotionEnabled && value("fps_limit") == "100")
        settings.set(smooth, "0")
        precondition(!settings.smoothMotionEnabled && value("fps_limit") == "game")
        print("PASS Swift graphics presets: persistence, unrelated settings, restart flags")
    }
}
