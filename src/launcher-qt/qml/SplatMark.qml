// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import QtQuick
import QtQuick.Effects

// A splat of ink: the white mask (dist/launcher) tinted where it is drawn, sized and turned
Item {
    id: root
    property string splat: "burst" // burst, drip, streak
    property color color: Theme.purple
    property real size: 60
    property real angle: 0
    width: size
    height: size
    rotation: angle
    Image {
        id: mask
        anchors.fill: parent
        source: "qrc:/inkypaint/assets/splat-" + root.splat + ".png"
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        visible: false
    }
    MultiEffect {
        anchors.fill: mask
        source: mask
        colorization: 1.0
        colorizationColor: root.color
        brightness: 0
        opacity: root.color.a
    }
}
