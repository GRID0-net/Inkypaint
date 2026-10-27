// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Online play on GRID0+: signing in, the patches the game needs for it, and the account's friends and requests
PageScroll {
    id: page
    readonly property var online: app.online
    readonly property var patches: online.patches
    cornerInk: Theme.magenta
    cornerSplat: "drip"
    cornerAngle: 160

    // while the page shows: the friends every 30 seconds, and the patches once
    readonly property bool showing: app.pane === "online"
    onShowingChanged: if (showing) page.visit()
    function visit() {
        if (!online.signedIn) return
        if (app.prepared && patches.status === "unknown") app.updatePatches()
        online.refresh()
    }
    Timer { interval: 30000; repeat: true; running: page.showing && online.signedIn; onTriggered: online.refresh() }

    Hero {
        title: online.signedIn ? "Online on GRID0+" : "Play online"
        subtitle: "Turf War, Salmon Run and your friends, on GRID0+, the community's servers. inkypaint talks to them with its own TLS: no console firmware needed."
        glyph: "◉"
        ink: Theme.magenta
        angle: 16
    }
    Banner { visible: online.error !== ""; text: online.error; Layout.fillWidth: true }
    Banner {
        visible: online.notice !== ""
        text: online.notice
        glyph: "✔"
        tint: Theme.lime
        dismissable: true
        onDismissed: online.dismissNotice()
        Layout.fillWidth: true
    }

    // ---- signing in
    SignInCard { visible: !online.signedIn; Layout.fillWidth: true }
    Connections {
        target: online
        function onChanged() {
            if (!online.signedIn) return
            if (page.showing && app.prepared && patches.status === "unknown") app.updatePatches()
        }
        function onRequestSent() { codeField.text = "" }
    }

    // ---- the account
    GlassCard {
        visible: online.signedIn
        Layout.fillWidth: true
        tint: Theme.purple
        ColumnLayout {
            width: parent.width
            spacing: 16
            RowLayout {
                Layout.fillWidth: true
                spacing: 14
                Avatar { name: online.me.nickname || online.username; pid: online.me.id || online.username; size: 56 }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3
                    InkText { text: online.me.nickname || online.username; kind: "title"; font.pixelSize: 22 }
                    InkText { text: "@" + (online.me.login || online.username); kind: "secondary" }
                }
                InkButton {
                    id: codeButton
                    visible: (online.me.friendCode || "") !== ""
                    property bool copied: false
                    text: copied ? "Copied" : online.me.friendCode || ""
                    iconText: copied ? "✔" : "⧉"
                    font.family: Theme.mono
                    onClicked: { online.copy(online.me.friendCode); copied = true; copyTimer.restart() }
                    Timer { id: copyTimer; interval: 1500; onTriggered: codeButton.copied = false }
                    ToolTip.visible: hovered
                    ToolTip.text: "Copy your friend code"
                }
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }
            RowLayout {
                Layout.fillWidth: true
                spacing: 16
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    InkText { text: "Play online"; kind: "headline" }
                    InkText { text: "The game signs in to GRID0+ as you when it starts. Off, it plays offline."; kind: "caption"; Layout.fillWidth: true }
                }
                InkSwitch { checked: online.playsOnline; enabled: !app.playing; onToggled: online.setPlaysOnline(checked) }
                InkButton { text: "Sign Out"; destructive: true; enabled: !app.playing; onClicked: online.signOut() }
            }
            InkText { visible: app.playing; text: "Changes here apply the next time the game starts."; kind: "caption" }
        }
    }

    // ---- the patches GRID0+ needs in the game's code, and the recompile that puts them there
    GlassCard {
        id: patchCard
        visible: online.signedIn && app.prepared
        readonly property bool recompile: patches.status !== "checking" && (app.settings.revision >= 0 && patches.tag.length >= 0 && app.patchesNeedRecompile())
        Layout.fillWidth: true
        tint: recompile ? Theme.lime : "transparent"
        RowLayout {
            width: parent.width
            spacing: 14
            Text {
                text: patchCard.recompile ? "🔨" : patches.status === "failed" ? "⚠" : patches.status === "current" ? "✔" : "🩹"
                color: patchCard.recompile ? Theme.lime : Theme.purple
                font.pixelSize: 22
                Layout.preferredWidth: 32
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                InkText { text: "Game patches"; kind: "headline" }
                InkText {
                    kind: "secondary"
                    Layout.fillWidth: true
                    text: app.preparer.running ? "Recompiling with the patches…"
                        : patches.status === "unknown" ? "GRID0+ patches the game's online client. Checking for them…"
                        : patches.status === "checking" ? "Checking GRID0+'s newest patches…"
                        : patches.status === "failed" ? patches.failure
                        : patchCard.recompile ? "Downloaded (" + patches.tag + "). The game needs a recompile to use them. Only the code is redone, about as long as the first time."
                        : "Up to date (" + patches.tag + "), and compiled into the game."
                }
            }
            BusyIndicator { visible: patches.status === "checking"; running: visible; implicitWidth: 28; implicitHeight: 28 }
            InkButton { visible: patches.status === "failed"; text: "Try Again"; onClicked: app.updatePatches() }
            InkButton {
                visible: patches.status !== "failed" && patches.status !== "checking" && patchCard.recompile
                prominent: true
                tint: Theme.lime
                text: "Recompile Now"
                enabled: !app.preparer.running && !app.playing
                onClicked: app.recompile()
            }
            InkButton {
                visible: patches.status !== "failed" && patches.status !== "checking" && !patchCard.recompile
                text: "Check for Updates"
                onClicked: app.updatePatches()
            }
        }
    }

    // ---- friends
    GlassCard {
        visible: online.signedIn
        Layout.fillWidth: true
        RowLayout {
            width: parent.width
            spacing: 12
            Text { text: "＋"; color: Theme.purple; font.pixelSize: 20 }
            InkField {
                id: codeField
                mono: true
                Layout.fillWidth: true
                placeholderText: "Friend code, SW-1234-5678-9012"
                onAccepted: online.sendRequest(text)
            }
            InkButton {
                text: "Send Request"
                enabled: codeField.text.replace(/\D/g, "").length === 12
                onClicked: online.sendRequest(codeField.text)
            }
        }
    }
    GlassCard {
        visible: online.signedIn && (online.incoming.length > 0 || online.outgoing.length > 0)
        Layout.fillWidth: true
        ColumnLayout {
            width: parent.width
            spacing: 12
            InkText { visible: online.incoming.length > 0; text: "Friend requests"; kind: "headline" }
            Repeater {
                model: online.incoming
                delegate: PlayerRow {
                    required property var modelData
                    player: modelData.other
                    subtitle: modelData.other.friendCode
                    InkButton { prominent: true; tint: Theme.lime; text: "Accept"; enabled: online.busy.indexOf(modelData.id) < 0
                        onClicked: online.accept(modelData.id, modelData.other.nickname) }
                    InkButton { text: "Decline"; enabled: online.busy.indexOf(modelData.id) < 0; onClicked: online.decline(modelData.id) }
                }
            }
            InkText { visible: online.outgoing.length > 0; text: "Sent"; kind: "headline" }
            Repeater {
                model: online.outgoing
                delegate: PlayerRow {
                    required property var modelData
                    player: modelData.other
                    subtitle: "Waiting for an answer"
                    InkButton { text: "Cancel"; enabled: online.busy.indexOf(modelData.id) < 0; onClicked: online.cancel(modelData.id) }
                }
            }
        }
    }
    GlassCard {
        visible: online.signedIn
        Layout.fillWidth: true
        ColumnLayout {
            width: parent.width
            spacing: 12
            RowLayout {
                Layout.fillWidth: true
                InkText { text: "Friends"; kind: "headline" }
                InkText {
                    visible: online.friends.length > 0
                    text: online.friends.filter(f => f.online).length + " online of " + online.friends.length
                    kind: "secondary"
                }
                Item { Layout.fillWidth: true }
                InkText { visible: online.refreshed !== ""; text: "Updated " + online.refreshed; kind: "caption"; color: Theme.tertiary }
                InkButton { text: "↻"; onClicked: online.refresh(); ToolTip.visible: hovered; ToolTip.text: "Refresh" }
            }
            ColumnLayout {
                visible: online.friends.length === 0
                Layout.fillWidth: true
                Layout.topMargin: 10
                Layout.bottomMargin: 10
                InkText { text: "No friends yet"; kind: "headline"; Layout.alignment: Qt.AlignHCenter }
                InkText { text: "Share your friend code, or send a request with someone else's."; kind: "secondary"; Layout.alignment: Qt.AlignHCenter }
            }
            Repeater {
                model: online.friends
                delegate: PlayerRow {
                    id: friendRow
                    required property var modelData
                    player: modelData
                    subtitle: modelData.activity
                    presence: true
                    Text { visible: modelData.favorite; text: "★"; color: "#facc15"; font.pixelSize: 16 }
                    InkButton { text: "⋯"; onClicked: friendMenu.popup() }
                    Menu {
                        id: friendMenu
                        MenuItem { text: "Copy Friend Code"; enabled: modelData.friendCode !== ""; onTriggered: online.copy(modelData.friendCode) }
                        MenuItem { text: "Remove Friend…"; onTriggered: { removeDialog.who = modelData; removeDialog.open() } }
                    }
                }
            }
        }
    }
    Dialog {
        id: removeDialog
        property var who: ({})
        anchors.centerIn: Overlay.overlay
        modal: true
        title: "Remove " + (who.nickname || "") + " from your friends?"
        standardButtons: Dialog.Ok | Dialog.Cancel
        Text { text: "You can send them a request again later." }
        onAccepted: online.remove(who.id, who.nickname)
    }
}
