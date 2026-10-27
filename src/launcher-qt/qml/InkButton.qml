// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import QtQuick
import QtQuick.Controls.Basic

// The app's button: prominent (filled with an ink), or glass (translucent)
Button {
    id: control
    property bool prominent: false
    property color tint: Theme.purple
    property bool large: false
    property bool destructive: false
    property string iconText: "" // a glyph before the text
    font.family: Theme.family
    font.pixelSize: large ? 18 : 14
    font.weight: prominent ? Font.Bold : Font.DemiBold
    leftPadding: large ? 26 : 16
    rightPadding: large ? 26 : 16
    topPadding: large ? 13 : 8
    bottomPadding: large ? 13 : 8
    hoverEnabled: true
    readonly property bool darkText: prominent && (tint.r * 0.299 + tint.g * 0.587 + tint.b * 0.114) > 0.6
    contentItem: Row {
        spacing: 8
        Text {
            visible: control.iconText !== ""
            text: control.iconText
            font: control.font
            color: label.color
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            id: label
            text: control.text
            font: control.font
            color: control.destructive && !control.prominent ? "#ff8a8a"
                 : control.darkText ? "#111" : Theme.text
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
        }
    }
    background: Rectangle {
        radius: height / 2
        color: control.prominent ? (control.down ? Qt.darker(control.tint, 1.2) : control.hovered ? Qt.lighter(control.tint, 1.08) : control.tint)
               : control.down ? Qt.rgba(1, 1, 1, 0.2) : control.hovered ? Qt.rgba(1, 1, 1, 0.15) : Qt.rgba(1, 1, 1, 0.09)
        border.color: control.prominent ? Qt.lighter(control.tint, 1.25) : Theme.glassBorder
        border.width: 1
    }
    opacity: enabled ? 1 : 0.45
}
