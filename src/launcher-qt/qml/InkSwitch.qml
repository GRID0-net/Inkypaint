// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import QtQuick
import QtQuick.Controls.Basic

AbstractButton {
    id: control
    hoverEnabled: true
    checkable: true
    padding: 0
    Accessible.role: Accessible.CheckBox
    background: Rectangle {
        implicitWidth: 46
        implicitHeight: 26
        radius: 13
        color: control.checked ? Theme.lime : Qt.rgba(1, 1, 1, 0.16)
        border.color: Theme.glassBorder
        Rectangle {
            x: control.checked ? parent.width - width - 3 : 3
            y: 3
            width: 20
            height: 20
            radius: 10
            color: control.checked ? "#16112b" : "#ddd"
            Behavior on x { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
        }
    }
    contentItem: Item {}
    // as wide as the track it draws, so a layout leaves it its room
    implicitWidth: 46
    implicitHeight: 26
    opacity: enabled ? 1 : 0.45
}
