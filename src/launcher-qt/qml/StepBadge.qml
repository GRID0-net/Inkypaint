// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import QtQuick

// A numbered step in ink: a small splat with the number on it, a check once it is done
Item {
    property int number: 1
    property color color: Theme.purple
    property bool done: false
    width: 40
    height: 40
    SplatMark { splat: "drip"; color: parent.done ? Theme.lime : parent.color; size: 40; angle: parent.number * 37 }
    Text {
        anchors.centerIn: parent
        text: parent.done ? "✓" : parent.number
        color: parent.done ? "#111" : "white"
        font.family: Theme.family
        font.pixelSize: 17
        font.weight: Font.Black
        font.italic: !parent.done
    }
}
