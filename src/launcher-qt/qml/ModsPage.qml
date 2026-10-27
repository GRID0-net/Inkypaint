// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

// The game's mods: adding one from a folder or an archive, turning each on or off, removing it. Files (romfs/) apply
// the next time the game starts; code (exefs/) takes a recompile, which the page offers when it is due
PageScroll {
    id: page
    readonly property var mods: app.mods
    cornerInk: Theme.lime
    cornerSplat: "burst"
    cornerAngle: -30

    readonly property bool showing: app.pane === "mods"
    onShowingChanged: if (showing) mods.refresh()

    Hero {
        title: "Mods"
        subtitle: "Files that change the game's models, textures, text or sounds work right away: no recompile. Mods that change the game's code need one."
        glyph: "✚"
        ink: Theme.lime
        angle: -12
    }
    Banner { visible: mods.error !== ""; text: mods.error; dismissable: true; onDismissed: mods.dismiss(); Layout.fillWidth: true }
    Banner {
        visible: mods.notice !== "" && mods.error === ""
        text: mods.notice
        glyph: "✔"
        tint: Theme.lime
        dismissable: true
        onDismissed: mods.dismiss()
        Layout.fillWidth: true
    }

    // ---- code mods changed since the game was compiled
    GlassCard {
        visible: mods.needsRecompile
        Layout.fillWidth: true
        tint: Theme.lime
        RowLayout {
            width: parent.width
            spacing: 14
            Text { text: "🔨"; color: Theme.lime; font.pixelSize: 22; Layout.preferredWidth: 32 }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                InkText { text: "Recompile for the code mods"; kind: "headline" }
                InkText {
                    text: app.preparer.running ? "Recompiling…"
                        : "The mods that change the game's code aren't compiled in yet. Until then the game runs without them (their files still apply)."
                    kind: "secondary"
                    Layout.fillWidth: true
                }
            }
            InkButton {
                prominent: true
                tint: Theme.lime
                text: "Recompile Now"
                enabled: !app.preparer.running && !app.playing && app.setupStep === "idle"
                onClicked: app.recompile()
            }
        }
    }

    // ---- adding
    DropArea {
        Layout.fillWidth: true
        implicitHeight: addCard.implicitHeight
        onDropped: (drop) => { for (const u of drop.urls) mods.add(u.toString()) }
        GlassCard {
            id: addCard
            width: parent.width
            tint: parent.containsDrag ? Theme.lime : "transparent"
            ColumnLayout {
                width: parent.width
                spacing: 12
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    InkButton { prominent: true; text: "Add Mod Folder…"; iconText: "✚"; enabled: !mods.busy && !app.playing; onClicked: folderDialog.open() }
                    InkButton { text: "Add Archive…"; enabled: !mods.busy && !app.playing; onClicked: archiveDialog.open() }
                    Item { Layout.fillWidth: true }
                    BusyIndicator { visible: mods.busy; running: visible; implicitWidth: 28; implicitHeight: 28 }
                    InkButton { text: "Show Mods Folder"; onClicked: mods.reveal() }
                }
                InkText {
                    text: mods.busy ? "Adding the mod…"
                        : "Or drop a mod here. inkypaint finds its romfs and exefs folders inside, including the atmosphere/contents/0100C2500FC20000 layout. Mods load in name order: where two change the same file, the later one wins."
                    kind: "caption"
                    Layout.fillWidth: true
                }
                InkText { visible: app.playing; text: "Changes apply the next time the game starts."; kind: "caption" }
            }
        }
    }

    // ---- the mods
    GlassCard {
        Layout.fillWidth: true
        ColumnLayout {
            width: parent.width
            spacing: 12
            InkText { text: "Installed"; kind: "headline" }
            ColumnLayout {
                visible: mods.mods.length === 0
                Layout.fillWidth: true
                Layout.topMargin: 10
                Layout.bottomMargin: 10
                InkText { text: "No mods yet"; kind: "headline"; Layout.alignment: Qt.AlignHCenter }
                InkText { text: "Add a folder or an archive above."; kind: "secondary"; Layout.alignment: Qt.AlignHCenter }
            }
            Repeater {
                model: mods.mods
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 14
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        InkText { text: modelData.name; kind: "headline"; elide: Text.ElideRight; wrapMode: Text.NoWrap; Layout.fillWidth: true }
                        InkText {
                            kind: "caption"
                            Layout.fillWidth: true
                            text: modelData.code && modelData.romfs ? "Files and code: the code takes a recompile"
                                : modelData.code ? "Code: takes a recompile"
                                : modelData.romfs ? "Files: no recompile needed"
                                : "Nothing inkypaint can use (no romfs or exefs)"
                        }
                    }
                    InkSwitch { checked: modelData.on; enabled: !app.playing; onToggled: mods.setOn(modelData.name, checked) }
                    InkButton { text: "Remove"; destructive: true; enabled: !app.playing && !mods.busy
                        onClicked: { removeDialog.name = modelData.name; removeDialog.open() } }
                }
            }
        }
    }

    FolderDialog {
        id: folderDialog
        title: "Choose a mod's folder"
        onAccepted: mods.add(selectedFolder.toString())
    }
    FileDialog {
        id: archiveDialog
        title: "Choose a mod's archive"
        nameFilters: ["Archives (*.zip *.7z *.rar)", "All files (*)"]
        onAccepted: mods.add(selectedFile.toString())
    }
    Dialog {
        id: removeDialog
        property string name: ""
        anchors.centerIn: Overlay.overlay
        modal: true
        title: "Remove " + name + "?"
        standardButtons: Dialog.Ok | Dialog.Cancel
        Text { text: "Its files are deleted from the mods folder." }
        onAccepted: mods.remove(name)
    }
}
