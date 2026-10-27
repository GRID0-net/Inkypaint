// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root
    property string text
    property string glyph: "⚠"
    property color tint: Theme.orange
    property bool dismissable: false
    signal dismissed()
    radius: 16
    color: Qt.rgba(tint.r, tint.g, tint.b, 0.16)
    border.color: Qt.rgba(tint.r, tint.g, tint.b, 0.4)
    implicitHeight: row.implicitHeight + 24
    RowLayout {
        id: row
        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 16 }
        spacing: 10
        Text { text: root.glyph; color: root.tint; font.pixelSize: 16 }
        Text {
            text: root.text
            color: Theme.text
            font.family: Theme.family
            font.pixelSize: 14
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Text {
            visible: root.dismissable
            text: "✕"
            color: Theme.secondary
            font.pixelSize: 14
            MouseArea { anchors.fill: parent; anchors.margins: -6; onClicked: root.dismissed(); cursorShape: Qt.PointingHandCursor }
        }
    }
}
