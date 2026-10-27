// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
pragma Singleton
import QtQuick

// Splatoon's inks, for accents on a quiet dark app (src/launcher's Ink)
QtObject {
    readonly property color lime: Qt.rgba(0.80, 0.95, 0.20, 1)
    readonly property color purple: Qt.rgba(0.36, 0.24, 0.96, 1)
    readonly property color magenta: Qt.rgba(0.93, 0.22, 0.78, 1)
    readonly property color night: Qt.rgba(0.07, 0.04, 0.17, 1)
    readonly property color dusk: Qt.rgba(0.17, 0.08, 0.42, 1)
    readonly property color orange: "#ff9f43"
    readonly property color text: "#f4f1ff"
    readonly property color secondary: Qt.rgba(0.96, 0.95, 1, 0.62)
    readonly property color tertiary: Qt.rgba(0.96, 0.95, 1, 0.4)
    readonly property color glass: Qt.rgba(1, 1, 1, 0.07)
    readonly property color glassBorder: Qt.rgba(1, 1, 1, 0.13)
    readonly property color field: Qt.rgba(0, 0, 0, 0.28)
    readonly property string family: Qt.platform.os === "windows" ? "Segoe UI" : "sans-serif"
    readonly property string mono: Qt.platform.os === "windows" ? "Cascadia Mono" : "monospace"
}
