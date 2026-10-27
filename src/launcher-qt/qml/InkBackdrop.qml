// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import QtQuick

// The Play screen's backdrop: a night-purple field with splats of the three inks drifting slowly
Item {
    id: root
    property real drift: 0
    SequentialAnimation on drift {
        loops: Animation.Infinite
        NumberAnimation { from: 0; to: 1; duration: 7000; easing.type: Easing.InOutSine }
        NumberAnimation { from: 1; to: 0; duration: 7000; easing.type: Easing.InOutSine }
    }
    readonly property real w: width
    readonly property real h: height
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: Theme.night }
            GradientStop { position: 1; color: Theme.dusk }
        }
    }
    Rectangle { // the glow behind the big splat
        x: root.w * 0.78 - width / 2
        y: root.h * 0.28 - height / 2
        width: Math.max(root.w, root.h) * 1.1
        height: width
        radius: width / 2
        gradient: Gradient {
            GradientStop { position: 0; color: Qt.rgba(0.36, 0.24, 0.96, 0.32) }
            GradientStop { position: 1; color: "transparent" }
        }
        opacity: 0.8
    }
    SplatMark { splat: "burst"; color: Theme.purple; size: root.h * 0.95; angle: -14 + 6 * root.drift
        x: root.w * 0.80 - size / 2; y: root.h * 0.34 - size / 2 + 10 - 20 * root.drift }
    SplatMark { splat: "streak"; color: Theme.lime; size: root.h * 0.62; angle: 18 + 6 * root.drift
        x: root.w * 0.97 - size / 2 - 6 + 12 * root.drift; y: root.h * 0.86 - size / 2 }
    SplatMark { splat: "drip"; color: Theme.magenta; size: root.h * 0.30; angle: 8
        x: root.w * 0.56 - size / 2; y: root.h * 0.10 - size / 2 - 4 + 8 * root.drift }
    SplatMark { splat: "streak"; color: Qt.rgba(0.93, 0.22, 0.78, 0.85); size: root.h * 0.16; angle: -30
        x: root.w * 0.30 - size / 2; y: root.h * 0.80 - size / 2 }
    SplatMark { splat: "burst"; color: Qt.rgba(0.80, 0.95, 0.20, 0.9); size: root.h * 0.12; angle: 50
        x: root.w * 0.45 - size / 2; y: root.h * 0.40 - size / 2 }
    Rectangle { // the text side stays readable
        anchors.fill: parent
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: Qt.rgba(0.07, 0.04, 0.17, 0.85) }
            GradientStop { position: 0.6; color: "transparent" }
        }
    }
}
