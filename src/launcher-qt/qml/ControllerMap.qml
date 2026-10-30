// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
ColumnLayout {
    Repeater {
        model: [
            { key: "south", title: "Bottom (A / Cross)" },
            { key: "east", title: "Right (B / Circle)" },
            { key: "west", title: "Left (X / Square)" },
            { key: "north", title: "Top (Y / Triangle)" },
            { key: "lb", title: "Left bumper" },
            { key: "rb", title: "Right bumper" },
            { key: "lt", title: "Left trigger" },
            { key: "rt", title: "Right trigger" },
            { key: "ls", title: "Left stick click" },
            { key: "rs", title: "Right stick click" },
            { key: "back", title: "Back / Share" },
            { key: "start", title: "Start / Options" },
            { key: "up", title: "D-pad up" },
            { key: "down", title: "D-pad down" },
            { key: "left", title: "D-pad left" },
            { key: "right", title: "D-pad right" }
        ]
        delegate: RowLayout {
            required property var modelData
            Layout.fillWidth: true
            InkText { text: modelData.title; Layout.fillWidth: true }
            InkChoice {
                options: ["A", "B", "X", "Y", "L", "R", "ZL", "ZR", "Plus", "Minus", "L3", "R3", "Up", "Down", "Left", "Right", "None"].map(v => ({value: v, label: v}))
                current: app.settings.revision >= 0 ? app.settings.controllerValue(modelData.key) : ""
                onPicked: value => app.settings.setControllerMapping(modelData.key, value)
            }
        }
    }
}
