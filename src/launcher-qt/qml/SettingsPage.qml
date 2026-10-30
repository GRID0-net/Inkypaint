// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The game's settings file, recompiled/0100C2500FC20000/0100C2500FC20000.ini in the data folder
PageScroll {
    id: page
    property string inputPage: ""
    cornerInk: Theme.magenta
    Hero {
        title: page.inputPage === "keyboard" ? "Keyboard input" : page.inputPage === "controller" ? "Controller input" : "Settings"
        subtitle: app.prepared || app.settings.exists ? "Saved in the game's settings file as you change them."
                                                       : "Settings belong to the prepared game: set it up in Game Files first."
        glyph: "⚙"
        ink: Theme.magenta
        angle: 14
    }
    Banner { visible: app.settings.error !== ""; text: app.settings.error; Layout.fillWidth: true }

    InkButton { visible: page.inputPage !== ""; text: "Back to Settings"; onClicked: app.pane = "settings" }
    Repeater {
        model: app.settings.groups.filter(g => page.inputPage === "keyboard" ? ["Keyboard and mouse", "Keys in menus"].includes(g.title) : page.inputPage === "controller" ? g.title === "Controller input" : !["Keyboard and mouse", "Keys in menus", "Controller input"].includes(g.title))
        delegate: GlassCard {
            required property var modelData
            objectName: modelData.title
            Layout.fillWidth: true
            enabled: app.prepared || app.settings.exists
            opacity: enabled ? 1 : 0.5
            ColumnLayout {
                width: parent.width
                spacing: 16
                InkText { text: modelData.title; kind: "title" }
                Flow {
                    visible: modelData.title === "Graphics"
                    Layout.fillWidth: true
                    spacing: 10
                    Repeater {
                        model: ["Performance", "Balanced", "Quality"]
                        delegate: InkButton {
                            required property string modelData
                            text: modelData
                            onClicked: app.settings.applyGraphicsPreset(modelData)
                        }
                    }
                }
                InkText {
                    visible: modelData.title === "Graphics"
                    text: "Settings marked next launch need a restart. Change individual settings below."
                    kind: "caption"
                    Layout.fillWidth: true
                }
                RowLayout {
                    visible: modelData.title === "Controls"
                    InkButton { text: "Change keyboard input"; onClicked: app.pane = "keyboardInput" }
                    InkButton { text: "Change controller input"; onClicked: app.pane = "controllerInput" }
                }
                Repeater {
                    model: modelData.settings
                    delegate: RowLayout {
                        id: row
                        required property var modelData
                        readonly property string current: app.settings.revision >= 0 ? app.settings.value(modelData.key) : ""
                        Layout.fillWidth: true
                        spacing: 16
                        ColumnLayout {
                            // the room the control leaves: its preferred width 0, so a long detail wraps instead
                            // of running under the control, and a row without one still pushes it to the right
                            Layout.fillWidth: true
                            Layout.preferredWidth: 0
                            spacing: 2
                            RowLayout {
                                spacing: 8
                                Layout.fillWidth: true
                                InkText { text: row.modelData.title; font.weight: Font.DemiBold; wrapMode: Text.NoWrap }
                                Rectangle {
                                    visible: row.modelData.nextLaunch
                                    radius: 8
                                    color: Qt.rgba(1, 1, 1, 0.1)
                                    implicitWidth: nl.implicitWidth + 12
                                    implicitHeight: nl.implicitHeight + 2
                                    InkText { id: nl; anchors.centerIn: parent; text: "next launch"; kind: "caption"; font.pixelSize: 10; font.weight: Font.DemiBold; wrapMode: Text.NoWrap }
                                }
                                Item { Layout.fillWidth: true }
                            }
                            InkText { visible: row.modelData.detail !== ""; text: row.modelData.detail; kind: "caption"; Layout.fillWidth: true }
                        }
                        Loader { active: row.modelData.kind === "controller_map"; visible: active; Layout.preferredWidth: 320; sourceComponent: Component { ControllerMap {} } }
                        InkSwitch {
                            Accessible.name: row.modelData.title
                            visible: row.modelData.kind === "toggle"
                            checked: row.current !== "0"
                            onToggled: app.settings.set(row.modelData.key, checked ? "1" : "0")
                        }
                        KeyCapture {
                            visible: row.modelData.kind === "key"
                            Layout.fillWidth: false
                            current: row.current
                            fallback: row.modelData.fallback || ""
                            onPicked: (value) => app.settings.set(row.modelData.key, value)
                        }
                        InkChoice {
                            objectName: row.modelData.key + "Choice"
                            enabled: row.modelData.key !== "fps_limit" || (app.settings.revision >= 0 && app.settings.value("smooth_motion") !== "0")
                            opacity: enabled ? 1 : 0.45
                            visible: row.modelData.kind !== "toggle" && row.modelData.kind !== "key" && row.modelData.kind !== "controller_map"
                            options: row.modelData.kind === "adapter" ? (app.settings.revision >= 0 ? app.settings.adapters() : []) : row.modelData.options
                            current: row.current
                            onPicked: (value) => app.settings.set(row.modelData.key, value)
                        }
                    }
                }
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        InkText { text: "The game reads them when it starts, except where they say otherwise."; kind: "caption"; Layout.fillWidth: true }
        InkButton { text: "Reset to Defaults…"; enabled: app.settings.exists; onClicked: confirmReset.open() }
    }
    Dialog {
        id: confirmReset
        anchors.centerIn: Overlay.overlay
        modal: true
        title: "Reset all game settings to their defaults?"
        standardButtons: Dialog.Reset | Dialog.Cancel
        onReset: { app.settings.resetAll(); close() }
    }
}
