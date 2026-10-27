// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import QtQuick
import QtQuick.Controls.Basic

TextField {
    id: control
    property bool mono: false
    color: Theme.text
    placeholderTextColor: Theme.tertiary
    font.family: mono ? Theme.mono : Theme.family
    font.pixelSize: 15
    leftPadding: 14
    rightPadding: 14
    topPadding: 10
    bottomPadding: 10
    selectByMouse: true
    selectionColor: Theme.purple
    background: Rectangle {
        radius: 12
        color: Theme.field
        border.color: control.activeFocus ? Theme.purple : Theme.glassBorder
        border.width: control.activeFocus ? 2 : 1
    }
}
