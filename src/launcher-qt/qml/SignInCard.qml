// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Signing in to GRID0+: on the Online page, and in the question setting up asks before it starts
GlassCard {
    id: card
    readonly property var online: app.online
    tint: Theme.purple
    ColumnLayout {
        width: parent.width
        spacing: 14
        InkText { text: "Sign in to GRID0+"; kind: "title" }
        InkField {
            id: userField
            Layout.fillWidth: true
            placeholderText: "User name"
            text: online.username
            enabled: online.account !== "signingIn"
            onAccepted: passwordField.forceActiveFocus()
        }
        InkField {
            id: passwordField
            Layout.fillWidth: true
            placeholderText: "Password"
            echoMode: TextInput.Password
            enabled: online.account !== "signingIn"
            onAccepted: card.signIn()
        }
        InkText {
            text: (serverField.visible ? "▾ " : "▸ ") + "Server"
            kind: "secondary"
            MouseArea { anchors.fill: parent; onClicked: serverField.visible = !serverField.visible; cursorShape: Qt.PointingHandCursor }
        }
        InkField {
            id: serverField
            visible: false
            mono: true
            Layout.fillWidth: true
            placeholderText: online.defaultServer
            text: online.server === online.defaultServer ? "" : online.server
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 16
            InkText {
                text: "No account yet? Use /register with the GRID0+ bot on the GRID0 Discord server. Your password stays on this PC, "
                    + (Qt.platform.os === "windows" ? "protected for your Windows account." : "readable only by your user.")
                kind: "caption"
                Layout.fillWidth: true
            }
            InkButton {
                prominent: true
                tint: Theme.lime
                text: online.account === "signingIn" ? "Signing In…" : "Sign In"
                enabled: userField.text !== "" && passwordField.text !== "" && online.account !== "signingIn"
                onClicked: card.signIn()
            }
        }
    }
    function signIn() {
        if (userField.text === "" || passwordField.text === "") return
        online.signIn(userField.text, passwordField.text, serverField.text)
    }
    // the password isn't kept in the field once it has signed in
    Connections {
        target: card.online
        function onChanged() { if (card.online.signedIn) passwordField.text = "" }
    }
}
