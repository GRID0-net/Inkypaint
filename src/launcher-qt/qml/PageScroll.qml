// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// A page's scrolling column, at most 760 wide and centered, over a quiet backdrop of two splats
Item {
    id: root
    default property alias content: column.data
    property color cornerInk: Theme.purple
    property color footInk: Theme.lime
    property string cornerSplat: "streak"
    property real cornerAngle: 20
    Rectangle { anchors.fill: parent; color: Theme.night }
    SplatMark { splat: root.cornerSplat; color: Qt.rgba(root.cornerInk.r, root.cornerInk.g, root.cornerInk.b, 0.18); size: 380; angle: root.cornerAngle
        x: root.width - 60 - size / 2; y: 70 - size / 2 }
    SplatMark { splat: "drip"; color: Qt.rgba(root.footInk.r, root.footInk.g, root.footInk.b, 0.13); size: 260; angle: -10
        x: 40 - size / 2; y: root.height - 40 - size / 2 }
    Flickable {
        id: flick
        anchors.fill: parent
        contentHeight: column.implicitHeight + 56
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        ColumnLayout {
            id: column
            spacing: 18
            width: Math.min(760, flick.width - 56)
            x: (flick.width - width) / 2
            y: 28
        }
    }
}
