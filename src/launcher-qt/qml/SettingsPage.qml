// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The game's settings file, recompiled/0100C2500FC20000/0100C2500FC20000.ini in the data folder
PageScroll {
    id: page
    cornerInk: Theme.magenta
    Hero {
        title: "Settings"
        subtitle: app.prepared || app.settings.exists ? "Saved in the game's settings file as you change them."
                                                       : "Settings belong to the prepared game: set it up in Game Files first."
        glyph: "⚙"
        ink: Theme.magenta
        angle: 14
    }
    Banner { visible: app.settings.error !== ""; text: app.settings.error; Layout.fillWidth: true }

    Repeater {
        model: app.settings.groups
        delegate: GlassCard {
            required property var modelData
            Layout.fillWidth: true
            enabled: app.prepared || app.settings.exists
            opacity: enabled ? 1 : 0.5
            ColumnLayout {
                width: parent.width
                spacing: 16
                InkText { text: modelData.title; kind: "title" }
                Repeater {
                    model: modelData.settings
                    delegate: RowLayout {
                        id: row
                        required property var modelData
                        readonly property string current: app.settings.revision >= 0 ? app.settings.value(modelData.key) : ""
                        Layout.fillWidth: true
                        spacing: 16
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            RowLayout {
                                spacing: 8
                                InkText { text: row.modelData.title; font.weight: Font.DemiBold; wrapMode: Text.NoWrap }
                                Rectangle {
                                    visible: row.modelData.nextLaunch
                                    radius: 8
                                    color: Qt.rgba(1, 1, 1, 0.1)
                                    implicitWidth: nl.implicitWidth + 12
                                    implicitHeight: nl.implicitHeight + 2
                                    InkText { id: nl; anchors.centerIn: parent; text: "next launch"; kind: "caption"; font.pixelSize: 10; font.weight: Font.DemiBold; wrapMode: Text.NoWrap }
                                }
                            }
                            InkText { visible: row.modelData.detail !== ""; text: row.modelData.detail; kind: "caption"; Layout.fillWidth: true }
                        }
                        InkSwitch {
                            visible: row.modelData.kind === "toggle"
                            checked: row.current !== "0"
                            onToggled: app.settings.set(row.modelData.key, checked ? "1" : "0")
                        }
                        InkChoice {
                            visible: row.modelData.kind !== "toggle"
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
        title: "Reset the game's settings shown here to their defaults?"
        standardButtons: Dialog.Reset | Dialog.Cancel
        onReset: { app.settings.resetAll(); close() }
    }
}
