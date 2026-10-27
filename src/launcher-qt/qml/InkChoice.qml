// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import QtQuick
import QtQuick.Controls.Basic

// A drop-down of {value, label} options, bound to one value
ComboBox {
    id: control
    property var options: []
    property string current: ""
    signal picked(string value)
    // a value set in the file by hand (or an adapter not connected now) stays selectable
    readonly property var shown: {
        for (let i = 0; i < options.length; i++) if (options[i].value === current) return options
        return options.concat([{ value: current, label: current === "" ? "Automatic" : current }])
    }
    model: shown
    textRole: "label"
    valueRole: "value"
    currentIndex: {
        for (let i = 0; i < shown.length; i++) if (shown[i].value === current) return i
        return -1
    }
    onActivated: (i) => picked(shown[i].value)
    implicitWidth: 260
    font.family: Theme.family
    font.pixelSize: 14
    contentItem: Text {
        leftPadding: 14
        rightPadding: 30
        text: control.displayText
        font: control.font
        color: Theme.text
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Text {
        x: control.width - width - 12
        y: (control.height - height) / 2
        text: "▾"
        color: Theme.secondary
        font.pixelSize: 14
    }
    background: Rectangle {
        implicitHeight: 38
        radius: 12
        color: control.hovered ? Qt.rgba(1, 1, 1, 0.14) : Qt.rgba(1, 1, 1, 0.08)
        border.color: Theme.glassBorder
    }
    popup: Popup {
        y: control.height + 4
        width: Math.max(control.width, 320)
        padding: 6
        implicitHeight: Math.min(contentItem.implicitHeight + 12, 420)
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
        }
        background: Rectangle { radius: 14; color: "#231a45"; border.color: Theme.glassBorder }
    }
    delegate: ItemDelegate {
        required property var modelData
        required property int index
        width: ListView.view ? ListView.view.width : 300
        highlighted: control.highlightedIndex === index
        contentItem: Text {
            text: modelData.label
            color: Theme.text
            font.family: Theme.family
            font.pixelSize: 14
            font.weight: index === control.currentIndex ? Font.Bold : Font.Normal
            elide: Text.ElideRight
        }
        background: Rectangle { radius: 10; color: highlighted ? Qt.rgba(0.36, 0.24, 0.96, 0.6) : "transparent" }
    }
}
