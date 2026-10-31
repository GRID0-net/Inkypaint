// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ApplicationWindow {
    id: window
    width: 1120
    height: 740
    minimumWidth: 900
    minimumHeight: 620
    visible: true
    title: "inkypaint"
    color: Theme.night
    font.family: Theme.family

    readonly property var panes: [
        { id: "play", title: "Play", glyph: "▶" },
        { id: "online", title: "Online", glyph: "◉" },
        { id: "settings", title: "Settings", glyph: "⚙" },
        { id: "mods", title: "Mods", glyph: "✚" },
        { id: "files", title: "Game Files", glyph: "▤" }
    ]

    // the game's settings and the preparation, read again when the window comes back to the front
    onActiveChanged: if (active && !app.playing) app.refresh()

    // Escape in the game, started from here: the launcher asks before the game closes (AppModel::quitRequested)
    Connections {
        target: app
        function onQuitRequested() {
            if (quitPopup.opened) return
            quitPopup.open()
            window.raise()
            window.requestActivate() // a Wayland compositor may refuse, the popup still waits
        }
        function onChanged() { // the game ended by itself: nothing left to ask
            if (!app.playing && quitPopup.opened) { quitPopup.answered = true; quitPopup.close() }
        }
    }

    Popup {
        id: quitPopup
        modal: true
        focus: true
        anchors.centerIn: Overlay.overlay
        width: 420
        padding: 0
        closePolicy: Popup.CloseOnEscape
        onClosed: if (!answered) app.answerQuit(false) // Escape: keep playing
        onAboutToShow: { answered = false; keepPlaying.forceActiveFocus() }
        property bool answered: false
        Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.55) }
        background: Rectangle { radius: 22; color: Theme.dusk; border.color: Theme.glassBorder; border.width: 1 }
        contentItem: ColumnLayout {
            spacing: 14
            InkText { text: "Close Splatoon 3?"; kind: "title"; Layout.fillWidth: true; Layout.margins: 24; Layout.bottomMargin: 0 }
            InkText { text: "The game will close."; kind: "secondary"; Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24 }
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 24
                Layout.topMargin: 6
                spacing: 10
                Item { Layout.fillWidth: true }
                InkButton {
                    text: "Close the game"
                    destructive: true
                    onClicked: { quitPopup.answered = true; quitPopup.close(); app.answerQuit(true) }
                }
                InkButton {
                    id: keepPlaying
                    text: "Keep playing"
                    prominent: true
                    onClicked: { quitPopup.answered = true; quitPopup.close(); app.answerQuit(false) }
                    Keys.onReturnPressed: clicked()
                    Keys.onEnterPressed: clicked()
                }
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle { // the sidebar
            Layout.fillHeight: true
            Layout.preferredWidth: 220
            color: Qt.rgba(0.10, 0.06, 0.22, 1)
            border.color: Qt.rgba(1, 1, 1, 0.06)
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                anchors.topMargin: 18
                spacing: 4
                Repeater {
                    model: window.panes
                    delegate: Rectangle {
                        required property var modelData
                        readonly property bool selected: app.pane === modelData.id || (modelData.id === "settings" && ["keyboardInput", "controllerInput"].includes(app.pane))
                        Layout.fillWidth: true
                        height: 42
                        radius: 12
                        color: selected ? Qt.rgba(0.36, 0.24, 0.96, 0.55) : hover.containsMouse ? Qt.rgba(1, 1, 1, 0.06) : "transparent"
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 14
                            anchors.rightMargin: 12
                            spacing: 12
                            Text { text: modelData.glyph; color: selected ? Theme.lime : Theme.secondary; font.pixelSize: 15 }
                            InkText { text: modelData.title; kind: "headline"; Layout.fillWidth: true; wrapMode: Text.NoWrap }
                            Rectangle { // the badge: the preparation's percent, or incoming requests
                                readonly property string label: modelData.id === "files" && app.preparer.running
                                        ? Math.round(app.preparer.fraction * 100) + "%"
                                        : modelData.id === "online" && app.online.incoming.length > 0 ? String(app.online.incoming.length) : ""
                                visible: label !== ""
                                radius: 9
                                color: Qt.rgba(1, 1, 1, 0.14)
                                implicitWidth: badge.implicitWidth + 14
                                implicitHeight: 20
                                Text { id: badge; anchors.centerIn: parent; text: parent.label; color: Theme.text; font.pixelSize: 12; font.weight: Font.Bold }
                            }
                        }
                        MouseArea { id: hover; anchors.fill: parent; hoverEnabled: true; onClicked: app.pane = modelData.id; cursorShape: Qt.PointingHandCursor }
                    }
                }
                Item { Layout.fillHeight: true }
                Row { // the app's name, inked
                    spacing: 8
                    Layout.margins: 6
                    SplatMark { splat: "drip"; color: Theme.purple; size: 30; angle: -18 }
                    InkText { text: "inkypaint"; kind: "ink"; inkSize: 20; anchors.verticalCenter: parent.verticalCenter }
                    Rectangle { width: 7; height: 7; radius: 3.5; color: Theme.lime; anchors.verticalCenter: parent.verticalCenter }
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: ["play", "online", "settings", "mods", "files", "keyboardInput", "controllerInput"].indexOf(app.pane)
            PlayPage {}
            OnlinePage {}
            SettingsPage {}
            ModsPage {}
            FilesPage {}
            SettingsPage { inputPage: "keyboard" }
            SettingsPage { inputPage: "controller" }
        }
    }
}
