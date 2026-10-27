// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import QtQuick
import QtQuick.Layouts

RowLayout {
    property string title
    property string hint
    property string path
    signal choose()
    Layout.fillWidth: true
    spacing: 12
    Text {
        text: path === "" ? "＋" : "✔"
        color: path === "" ? Theme.secondary : Theme.lime
        font.pixelSize: 18
        Layout.preferredWidth: 26
        horizontalAlignment: Text.AlignHCenter
    }
    ColumnLayout {
        spacing: 2
        Layout.fillWidth: true
        InkText { text: title; font.weight: Font.DemiBold; Layout.fillWidth: true }
        InkText {
            text: path === "" ? hint : path.split(/[\\/]/).pop()
            kind: "caption"
            elide: Text.ElideMiddle
            wrapMode: Text.NoWrap
            Layout.fillWidth: true
        }
    }
    InkButton { text: path === "" ? "Choose…" : "Change…"; onClicked: parent.choose() }
}
