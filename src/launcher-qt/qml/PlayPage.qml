// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import QtQuick
import QtQuick.Layouts

Item {
    id: page
    InkBackdrop { anchors.fill: parent }

    InkButton { // the game's folder, top right
        anchors { top: parent.top; right: parent.right; margins: 18 }
        text: "Show Game Folder"
        iconText: "📁"
        enabled: app.prepared
        onClicked: app.revealGameFolder()
    }

    ColumnLayout {
        anchors { left: parent.left; bottom: parent.bottom; margins: 36 }
        width: Math.min(620, parent.width - 72)
        spacing: 18

        Rectangle { // the version and the state
            radius: height / 2
            color: Qt.rgba(1, 1, 1, 0.1)
            border.color: Theme.glassBorder
            implicitWidth: status.implicitWidth + 24
            implicitHeight: status.implicitHeight + 12
            InkText {
                id: status
                anchors.centerIn: parent
                text: "<b>VERSION " + app.gameVersion + "</b>  ·  " + app.statusLine
                textFormat: Text.StyledText
                kind: "caption"
                color: Qt.rgba(1, 1, 1, 0.9)
                wrapMode: Text.NoWrap
            }
        }

        Image { // the game's logo, when the launcher was packaged with one
            visible: app.logo.toString() !== ""
            source: app.logo
            fillMode: Image.PreserveAspectFit
            Layout.maximumWidth: 440
            Layout.preferredWidth: 440
            Layout.preferredHeight: visible ? implicitHeight * 440 / Math.max(1, implicitWidth) : 0
            Layout.leftMargin: -12
            smooth: true
            mipmap: true
        }
        Text { // else its name in type
            visible: app.logo.toString() === ""
            text: app.gameName
            color: "white"
            font.family: Theme.family
            font.pixelSize: 68
            font.weight: Font.Black
            font.italic: true
            rotation: -3
            transformOrigin: Item.Left
            style: Text.Raised
            styleColor: Theme.purple
        }

        Row {
            spacing: 12
            InkButton {
                visible: !app.prepared
                prominent: true
                large: true
                tint: Theme.purple
                text: "Set Up Splatoon 3"
                iconText: "⬇"
                onClicked: app.pane = "files"
            }
            Rectangle {
                visible: app.prepared && app.playing
                radius: height / 2
                color: Qt.rgba(0.80, 0.95, 0.20, 0.3)
                border.color: Qt.rgba(0.80, 0.95, 0.20, 0.6)
                implicitWidth: playingLabel.implicitWidth + 44
                implicitHeight: 48
                InkText { id: playingLabel; anchors.centerIn: parent; text: "🎮  Playing"; kind: "title"; wrapMode: Text.NoWrap }
            }
            InkButton {
                visible: app.prepared && app.playing
                large: true
                destructive: true
                text: "Quit Game"
                iconText: "■"
                onClicked: app.stop()
            }
            Item {
                visible: app.prepared && !app.playing
                implicitWidth: playButton.implicitWidth
                implicitHeight: playButton.implicitHeight
                SplatMark { // the burst of ink when the game starts
                    id: burst
                    anchors.centerIn: parent
                    splat: "burst"
                    color: Theme.lime
                    size: 420
                    scale: 0.1
                    opacity: 0
                    ParallelAnimation {
                        id: burstAnim
                        NumberAnimation { target: burst; property: "scale"; from: 0.1; to: 1; duration: 450; easing.type: Easing.OutBack }
                        SequentialAnimation {
                            PropertyAction { target: burst; property: "opacity"; value: 1 }
                            PauseAnimation { duration: 350 }
                            NumberAnimation { target: burst; property: "opacity"; to: 0; duration: 900; easing.type: Easing.OutCubic }
                        }
                    }
                }
                InkButton {
                    id: playButton
                    prominent: true
                    large: true
                    tint: Theme.lime
                    text: "Play"
                    iconText: "▶"
                    font.pixelSize: 22
                    font.weight: Font.Black
                    onClicked: { burst.angle = Math.random() * 360; burstAnim.restart(); app.play() }
                }
            }
            InkButton {
                visible: app.prepared && !app.playing
                large: true
                text: "⚙"
                onClicked: app.pane = "settings"
            }
        }

        Flow { // the settings that matter most, at a glance
            visible: app.prepared
            Layout.fillWidth: true
            spacing: 10
            Repeater {
                model: [
                    { title: "Resolution", value: app.settings.revision >= 0 ? app.settings.label("resolution") : "" },
                    { title: "Frame rate", value: app.settings.revision >= 0 ? app.settings.label("fps_limit") : "" },
                    { title: "Shaders", value: app.settings.revision >= 0 && app.settings.value("async_pipelines") !== "0" ? "In the background" : "Waited for" },
                    { title: "Online", value: app.online.playsOnline ? "GRID0+" : "Offline" }
                ]
                delegate: Rectangle {
                    required property var modelData
                    radius: 16
                    color: Qt.rgba(1, 1, 1, 0.08)
                    border.color: Theme.glassBorder
                    implicitWidth: Math.max(140, tileCol.implicitWidth + 28)
                    implicitHeight: tileCol.implicitHeight + 20
                    Column {
                        id: tileCol
                        anchors { left: parent.left; verticalCenter: parent.verticalCenter; leftMargin: 14 }
                        spacing: 4
                        InkText { text: modelData.title; kind: "caption"; font.weight: Font.DemiBold; wrapMode: Text.NoWrap }
                        InkText { text: modelData.value; font.weight: Font.Bold; wrapMode: Text.NoWrap }
                    }
                }
            }
        }

        Banner {
            visible: app.lastExit !== ""
            text: app.lastExit
            Layout.fillWidth: true
        }

        Row {
            spacing: 10
            InkSwitch { checked: app.hideWhilePlaying; onToggled: app.hideWhilePlaying = checked }
            InkText { text: "Minimize the launcher while playing"; kind: "secondary"; anchors.verticalCenter: parent.verticalCenter; wrapMode: Text.NoWrap }
        }
    }
}
