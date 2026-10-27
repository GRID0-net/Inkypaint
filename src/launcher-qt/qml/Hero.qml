// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import QtQuick
import QtQuick.Layouts

// A page's heading: an inked splat with a glyph, the title and a line under it
RowLayout {
    property string title
    property string subtitle
    property string glyph
    property color ink: Theme.purple
    property real angle: -12
    Layout.fillWidth: true
    Layout.bottomMargin: 4
    spacing: 16
    Item {
        width: 74
        height: 74
        SplatMark { splat: "burst"; color: ink; size: 74; angle: parent.parent.angle }
        Text { anchors.centerIn: parent; text: glyph; color: "white"; font.pixelSize: 24 }
    }
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 4
        InkText { text: title; kind: "ink"; inkSize: 30; Layout.fillWidth: true }
        InkText { text: subtitle; kind: "secondary"; Layout.fillWidth: true }
    }
}
