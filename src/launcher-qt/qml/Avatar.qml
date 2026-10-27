// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import QtQuick

// A player's initial on a splat of their own colour
Item {
    id: root
    property string name: ""
    property string pid: ""
    property real size: 38
    width: size
    height: size
    readonly property var colors: [Theme.purple, Theme.magenta, "#20b2aa", "#ff9f43", "#5b4bd6", "#ff6fae"]
    function sum(s) { let n = 0; for (let i = 0; i < s.length; i++) n += s.charCodeAt(i); return n }
    SplatMark { splat: "burst"; color: root.colors[root.sum(root.pid) % root.colors.length]; size: root.size; angle: root.pid.length * 29 }
    Text {
        anchors.centerIn: parent
        text: root.name.length ? root.name.charAt(0).toUpperCase() : "?"
        color: "white"
        font.family: Theme.family
        font.pixelSize: root.size * 0.42
        font.weight: Font.Black
        font.italic: true
    }
}
