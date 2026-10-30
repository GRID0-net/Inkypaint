// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// A control's key (key_<id>): click, then press a key, a mouse button or turn the wheel. Esc keeps what it was. ↺ goes
// back to the game's default (empty), ✕ leaves the control without a key (None)
RowLayout {
    id: root
    property string current: ""   // what the file holds: "" is the default
    property string fallback: ""  // the default's keys
    signal picked(string value)
    spacing: 6

    Rectangle {
        id: field
        property bool listening: false
        implicitWidth: 200
        implicitHeight: 36
        radius: 10
        color: listening ? Qt.rgba(0.80, 0.95, 0.20, 0.22) : area.containsMouse ? Qt.rgba(1, 1, 1, 0.12) : Theme.field
        border.color: listening ? Theme.lime : Theme.glassBorder
        focus: listening
        Text {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            font.family: Theme.family
            font.pixelSize: 14
            color: field.listening || root.current !== "" ? Theme.text : Theme.secondary
            text: field.listening ? "Press a key or button…"
                : root.current === "None" ? "No key"
                : root.current !== "" ? root.current : root.fallback
        }
        function stop() { listening = false; focus = false }
        Keys.onPressed: (event) => {
            if (!listening) return
            event.accepted = true
            if (event.key === Qt.Key_Escape) { stop(); return }
            const name = app.settings.keyName(event.key, event.nativeScanCode, event.modifiers)
            if (name !== "") { root.picked(name); stop() }
        }
        MouseArea {
            id: area
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.AllButtons
            cursorShape: Qt.PointingHandCursor
            onPressed: (mouse) => {
                if (!field.listening) {
                    if (mouse.button === Qt.LeftButton) { field.listening = true; field.forceActiveFocus() }
                    return
                }
                const names = { [Qt.LeftButton]: "Mouse Left", [Qt.RightButton]: "Mouse Right", [Qt.MiddleButton]: "Mouse Middle",
                                [Qt.BackButton]: "Mouse X1", [Qt.ForwardButton]: "Mouse X2" }
                if (names[mouse.button] !== undefined) { root.picked(names[mouse.button]); field.stop() }
            }
            onWheel: (wheel) => {
                if (!field.listening || wheel.angleDelta.y === 0) return
                root.picked(wheel.angleDelta.y > 0 ? "Wheel Up" : "Wheel Down")
                field.stop()
            }
        }
        onActiveFocusChanged: if (!activeFocus) listening = false
    }
    InkButton {
        text: "↺"
        enabled: root.current !== ""
        onClicked: root.picked("")
        ToolTip.visible: hovered
        ToolTip.text: "The default: " + root.fallback
    }
    InkButton {
        text: "✕"
        enabled: root.current !== "None"
        onClicked: root.picked("None")
        ToolTip.visible: hovered
        ToolTip.text: "No key"
    }
}
