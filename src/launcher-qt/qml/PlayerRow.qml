// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import QtQuick
import QtQuick.Layouts

// A player in a list: their picture, name, a line under it, and what goes at the end (the trailing property)
RowLayout {
    id: root
    property var player: ({})
    property string subtitle
    property bool presence: false
    default property alias trailing: end.data
    Layout.fillWidth: true
    spacing: 12
    Item {
        width: 38
        height: 38
        Avatar { name: root.player.nickname || root.player.login || ""; pid: root.player.id || ""; size: 38 }
        Rectangle {
            visible: root.presence
            width: 12; height: 12; radius: 6
            x: 28; y: 28
            color: root.player.playing ? Theme.lime : root.player.online ? "#3b82f6" : Qt.rgba(0.6, 0.6, 0.6, 0.6)
            border.color: Theme.night
            border.width: 2
        }
    }
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 2
        InkText { text: root.player.nickname || root.player.login || ""; font.weight: Font.DemiBold; wrapMode: Text.NoWrap; elide: Text.ElideRight; Layout.fillWidth: true }
        InkText { text: root.subtitle; kind: "caption"; color: root.presence && root.player.playing ? Theme.lime : Theme.secondary; wrapMode: Text.NoWrap }
    }
    Row { id: end; spacing: 8 }
}
