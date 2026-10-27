// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

// Setting the game up: the dumps and keys to start from, where it goes, then extracting and recompiling it. Once it
// is ready, where it is and what can be done with it
PageScroll {
    id: page
    readonly property var prep: app.preparer
    readonly property bool downloading: app.setupStep === "downloadingPatches"
    readonly property bool showProgress: prep.running || prep.phase !== "idle" || downloading

    Hero {
        title: app.prepared ? "Splatoon 3 is ready" : "Set up Splatoon 3"
        subtitle: app.prepared ? "Recompiled for this PC, from your own copy of the game."
                               : "inkypaint turns your copy of the game into an app for this PC. You need the game, its 11.3.0 update and your console's keys."
        glyph: "▤"
    }

    // ---- setup
    ColumnLayout {
        visible: !page.showProgress && !app.prepared
        Layout.fillWidth: true
        spacing: 14

        DropArea { // files or folders dropped anywhere on the steps
            Layout.fillWidth: true
            implicitHeight: steps.implicitHeight
            onDropped: (drop) => { for (const u of drop.urls) app.sources.take(u.toString()) }
            ColumnLayout {
                id: steps
                width: parent.width
                spacing: 14
                GlassCard {
                    Layout.fillWidth: true
                    ColumnLayout {
                        width: parent.width
                        spacing: 14
                        RowLayout { spacing: 12
                            StepBadge { number: 1; done: app.sources.base !== "" && app.sources.update !== "" }
                            InkText { text: "The game"; kind: "title" } }
                        FileRow { title: "Splatoon 3"; hint: "The base game's NSP (0100C2500FC20000)"; path: app.sources.base; onChoose: page.pick("base") }
                        FileRow { title: "Update 11.3.0"; hint: "The update's NSP (0100C2500FC20800, v2752512)"; path: app.sources.update; onChoose: page.pick("update") }
                    }
                }
                GlassCard {
                    Layout.fillWidth: true
                    ColumnLayout {
                        width: parent.width
                        spacing: 14
                        RowLayout { spacing: 12
                            StepBadge { number: 2; done: app.sources.prodKeys !== "" }
                            InkText { text: "Your keys"; kind: "title" } }
                        FileRow { title: "prod.keys"; hint: "Dumped from your console"; path: app.sources.prodKeys; onChoose: page.pick("prodKeys") }
                        FileRow { title: "title.keys"; hint: "Optional"; path: app.sources.titleKeys; onChoose: page.pick("titleKeys") }
                    }
                }
                GlassCard {
                    Layout.fillWidth: true
                    ColumnLayout {
                        width: parent.width
                        spacing: 10
                        RowLayout { spacing: 12
                            StepBadge { number: 3; done: true }
                            InkText { text: "Where it goes"; kind: "title" } }
                        RowLayout {
                            Layout.fillWidth: true
                            InkText { text: "📁  " + app.dataFolder; font.family: Theme.mono; elide: Text.ElideMiddle; wrapMode: Text.NoWrap; Layout.fillWidth: true }
                            InkButton { text: "Change…"; onClicked: dataDialog.open() }
                        }
                        InkText { text: app.spaceLine; kind: "caption" }
                    }
                }
            }
        }
        Row {
            spacing: 12
            InkButton {
                prominent: true
                large: true
                text: "Prepare Splatoon 3"
                iconText: "✦"
                enabled: (app.sources.complete || app.extracted) && app.setupStep === "idle"
                onClicked: app.requestPrepare()
            }
            InkButton { text: "Find in a Folder…"; anchors.verticalCenter: parent.verticalCenter; onClicked: scanDialog.open() }
        }
        InkText { text: "Most of an hour on a typical PC, and 13 GB of disk. The PC stays usable meanwhile. Drop the files here, or a folder that holds them."; kind: "caption"; Layout.fillWidth: true }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder; Layout.topMargin: 6; Layout.bottomMargin: 6 }
        InkText { text: "Prepared the game already?"; kind: "headline" }
        InkText { text: "Choose its data folder to play it from here."; kind: "secondary"; Layout.fillWidth: true }
        InkButton { text: "Use an Existing Data Folder…"; onClicked: dataDialog.open() }
        InkText {
            visible: page.dataMessage !== ""
            text: page.dataMessage
            color: Theme.orange
            Layout.fillWidth: true
        }
    }

    // ---- preparing
    ColumnLayout {
        visible: page.showProgress
        Layout.fillWidth: true
        spacing: 14
        GlassCard {
            Layout.fillWidth: true
            tint: prep.phase === "done" ? Theme.lime : prep.phase === "failed" ? Theme.orange : "transparent"
            ColumnLayout {
                width: parent.width
                spacing: 14
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    StepBadge { number: prep.phase === "recompiling" ? 2 : 1; color: Theme.magenta; done: prep.phase === "done" }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        InkText {
                            kind: "title"
                            text: page.downloading ? "Getting GRID0+'s patches" : ({ idle: "Getting ready", extracting: "Extracting the game", recompiling: "Recompiling for this PC",
                                     done: "All done", failed: "Preparing stopped" })[prep.phase]
                        }
                        InkText {
                            text: page.downloading ? "Downloading them first, so the game is compiled with them once." : prep.detail
                            kind: "secondary"
                            Layout.fillWidth: true
                        }
                    }
                    BusyIndicator { visible: page.downloading; running: visible; implicitWidth: 28; implicitHeight: 28 }
                    InkText { visible: prep.running; text: Math.round(prep.fraction * 100) + "%"; kind: "ink"; inkSize: 26 }
                }
                ProgressBar {
                    visible: prep.running
                    Layout.fillWidth: true
                    value: prep.fraction
                    background: Rectangle { implicitHeight: 8; radius: 4; color: Qt.rgba(1, 1, 1, 0.12) }
                    contentItem: Item {
                        Rectangle { width: parent.width * prep.fraction; height: parent.height; radius: 4; color: Theme.lime }
                    }
                }
                InkText {
                    visible: prep.running && prep.started !== ""
                    text: "Started " + prep.started + ". You can leave this running."
                    kind: "caption"
                }
                InkText { visible: prep.phase === "failed"; text: prep.failure; color: Theme.orange; Layout.fillWidth: true }
                RowLayout {
                    Layout.fillWidth: true
                    InkButton { visible: prep.running; text: "Stop"; onClicked: prep.cancel() }
                    InkButton {
                        visible: prep.phase === "done"
                        prominent: true
                        tint: Theme.lime
                        text: "Play Splatoon 3"
                        iconText: "▶"
                        onClicked: { prep.reset(); app.refresh(); app.pane = "play" }
                    }
                    InkButton { visible: prep.phase === "failed"; prominent: true; text: "Try Again"; onClicked: app.prepare() }
                    InkButton { visible: prep.phase === "failed"; text: "Back"; onClicked: prep.reset() }
                    Item { Layout.fillWidth: true }
                    InkButton { text: details.visible ? "Hide Details" : "Details"; onClicked: details.visible = !details.visible }
                }
            }
        }
        Rectangle {
            id: details
            visible: false
            Layout.fillWidth: true
            Layout.preferredHeight: 240
            radius: 16
            color: Qt.rgba(0, 0, 0, 0.35)
            ListView {
                id: logView
                anchors.fill: parent
                anchors.margins: 12
                clip: true
                model: prep.log
                onCountChanged: positionViewAtEnd()
                delegate: Text {
                    required property string modelData
                    width: logView.width
                    text: modelData
                    color: Theme.secondary
                    font.family: Theme.mono
                    font.pixelSize: 12
                    wrapMode: Text.WrapAnywhere
                }
                ScrollBar.vertical: ScrollBar {}
            }
        }
    }

    // ---- ready
    ColumnLayout {
        visible: !page.showProgress && app.prepared
        Layout.fillWidth: true
        spacing: 14
        GlassCard {
            Layout.fillWidth: true
            tint: Theme.lime
            ColumnLayout {
                width: parent.width
                spacing: 12
                RowLayout { InkText { text: "Game"; kind: "secondary"; Layout.preferredWidth: 110 } InkText { text: app.gameName + " " + app.gameVersion } }
                RowLayout {
                    Layout.fillWidth: true
                    InkText { text: "Data folder"; kind: "secondary"; Layout.preferredWidth: 110 }
                    InkText { text: app.dataFolder; font.family: Theme.mono; elide: Text.ElideMiddle; wrapMode: Text.NoWrap; Layout.fillWidth: true }
                }
                Row {
                    spacing: 10
                    InkButton { text: Qt.platform.os === "windows" ? "Show in Explorer" : "Show in Files"; onClicked: app.revealGameFolder() }
                    InkButton { text: "Show Game Log"; onClicked: app.revealGameLog() }
                }
            }
        }
        GlassCard {
            Layout.fillWidth: true
            ColumnLayout {
                width: parent.width
                spacing: 10
                InkText { text: "Recompile"; kind: "headline" }
                InkText {
                    text: "After an update of inkypaint that changes how games are translated, the game is recompiled from its extraction."
                    kind: "secondary"
                    Layout.fillWidth: true
                }
                Row {
                    spacing: 10
                    InkButton { text: "Recompile…"; enabled: app.extracted && app.setupStep === "idle"; onClicked: confirmRecompile.open() }
                    InkButton { text: "Use Another Data Folder…"; onClicked: dataDialog.open() }
                }
            }
        }
    }

    property string dataMessage: ""
    property string slot: ""
    function pick(which) { slot = which; fileDialog.open() }

    FileDialog {
        id: fileDialog
        title: "Choose a file"
        nameFilters: page.slot === "base" || page.slot === "update" ? ["Switch packages (*.nsp)", "All files (*)"]
                   : ["Keys (*.keys)", "All files (*)"]
        onAccepted: app.sources.setSlot(page.slot, selectedFile.toString())
    }
    FolderDialog {
        id: scanDialog
        title: "Choose the folder that holds the game and the keys"
        onAccepted: app.sources.scan(selectedFolder.toString())
    }
    FolderDialog {
        id: dataDialog
        title: "Choose a data folder"
        onAccepted: {
            app.useDataFolder(selectedFolder.toString())
            page.dataMessage = app.prepared ? "" : "No prepared game there yet: preparing puts it in this folder."
        }
    }
    Dialog {
        id: confirmRecompile
        anchors.centerIn: Overlay.overlay
        modal: true
        title: "Recompile Splatoon 3?"
        standardButtons: Dialog.Ok | Dialog.Cancel
        InkText { text: "The game can't be played until it finishes."; color: "#222" }
        onAccepted: app.requestPrepare()
    }

    // ---- before setting up: play online? Signed in, GRID0+'s patches are downloaded first and compiled in with the
    // game, instead of a second recompile after signing in later
    Popup {
        id: onlineFirst
        anchors.centerIn: Overlay.overlay
        width: Math.min(560, Overlay.overlay ? Overlay.overlay.width - 48 : 560)
        modal: true
        padding: 24
        closePolicy: Popup.CloseOnEscape
        onClosed: app.cancelSetup()
        // opened and closed by the question's state (a binding on visible wouldn't survive Escape)
        Connections {
            target: app
            function onChanged() {
                const asking = app.setupStep === "askingOnline"
                if (asking && !onlineFirst.opened) onlineFirst.open()
                else if (!asking && onlineFirst.opened) onlineFirst.close()
            }
        }
        background: Rectangle {
            radius: 24
            gradient: Gradient { GradientStop { position: 0; color: Theme.dusk } GradientStop { position: 1; color: Theme.night } }
            border.color: Theme.glassBorder
        }
        contentItem: ColumnLayout {
            spacing: 14
            InkText { text: "Play online?"; kind: "title" }
            InkText {
                text: "Online play on GRID0+ needs patches compiled into the game. Sign in now and they're downloaded before the recompile, so it only happens once. Without them, adding online later means recompiling again."
                kind: "secondary"
                Layout.fillWidth: true
            }
            SignInCard { Layout.fillWidth: true }
            InkText { visible: app.online.error !== ""; text: app.online.error; color: Theme.orange; Layout.fillWidth: true }
            RowLayout {
                Layout.fillWidth: true
                InkButton { text: "Cancel"; onClicked: app.cancelSetup() }
                Item { Layout.fillWidth: true }
                InkButton { text: "Set Up Without Online"; onClicked: app.prepareOffline() }
            }
        }
    }
}
