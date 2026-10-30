// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
#include "gamesettings.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDebug>
#define CHECK(x) do { if (!(x)) { qCritical() << __LINE__ << #x; return 1; } } while (0)
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    CHECK(dir.isValid());
    GameSettings settings;
    const QString path = dir.filePath("game.ini");
    settings.setPath(path);
    settings.setRaw("grid0_user", "fixture-user", "Network");
    settings.setRaw("key_a", "K", "Keyboard and mouse");
    int changes = 0;
    QObject::connect(&settings, &GameSettings::changed, [&] { changes++; });
    settings.applyGraphicsPreset("Performance");
    CHECK(changes == 1 && settings.error().isEmpty());
    CHECK(settings.value("resolution") == "0.75" && settings.value("dynamic_resolution") == "1");
    CHECK(settings.value("shadows") == "0" && settings.value("texture_lod_bias") == "1");
    CHECK(settings.value("smooth_motion") == "0" && settings.value("fps_limit") == "game");
    CHECK(settings.value("anisotropy") == "1" && settings.value("fog") == "1");
    settings.applyGraphicsPreset("Balanced");
    CHECK(changes == 2 && settings.value("resolution") == "1");
    CHECK(settings.value("dynamic_resolution_min") == "0.625" && settings.value("shadows") == "1");
    CHECK(settings.value("texture_lod_bias") == "0" && settings.value("depth_of_field") == "0");
    settings.applyGraphicsPreset("Quality");
    CHECK(changes == 3 && settings.value("resolution") == "1.5");
    CHECK(settings.value("dynamic_resolution") == "0" && settings.value("anisotropy") == "16");
    CHECK(settings.value("depth_of_field") == "1");
    settings.applyGraphicsPreset("unknown");
    CHECK(changes == 3);
    GameSettings reopened;
    reopened.setPath(path);
    CHECK(reopened.value("resolution") == "1.5" && reopened.raw("grid0_user") == "fixture-user");
    CHECK(reopened.raw("key_a") == "K");
    reopened.setControllerMapping("south", "A");
    reopened.setControllerMapping("lt", "ZR");
    CHECK(reopened.controllerValue("south") == "A" && reopened.controllerValue("lt") == "ZR");
    CHECK(reopened.controllerValue("east") == "A");
    reopened.setControllerMapping("south", "B");
    CHECK(reopened.controllerValue("south") == "B" && reopened.controllerValue("lt") == "ZR");
    reopened.set("keyboard_mouse_enabled", "0");
    reopened.set("gamepad_menu_layout", "1");
    GameSettings inputs;
    inputs.setPath(path);
    CHECK(inputs.controllerValue("lt") == "ZR" && inputs.value("keyboard_mouse_enabled") == "0");
    CHECK(inputs.value("gamepad_menu_layout") == "1");
    int restartFlags = 0;
    for (const auto& group : reopened.groups())
        for (const auto& item : group.toMap().value("settings").toList()) {
            auto def = item.toMap();
            if (def.value("key") == "bloom" || def.value("key") == "shadows" || def.value("key") == "depth_of_field")
                if (def.value("nextLaunch").toBool()) restartFlags++;
        }
    CHECK(restartFlags == 3);
    settings.set("smooth_motion", "1");
    settings.set("fps_limit", "100");
    settings.set("smooth_motion", "0");
    CHECK(settings.value("fps_limit") == "game");
    qInfo() << "PASS launcher graphics presets: atomic publication, persistence, unrelated settings, restart flags";
    return 0;
}
