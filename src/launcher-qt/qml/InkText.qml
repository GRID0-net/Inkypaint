// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import QtQuick

// Text in the app's type: kind body, secondary, caption, headline, title (bold), ink (the game's loud display type,
// black and slanted)
Text {
    property string kind: "body"
    property real inkSize: 30
    color: kind === "secondary" || kind === "caption" ? Theme.secondary : Theme.text
    font.family: Theme.family
    font.pixelSize: kind === "caption" ? 12 : kind === "headline" ? 16 : kind === "title" ? 20 : kind === "ink" ? inkSize : 14
    font.weight: kind === "ink" ? Font.Black : kind === "title" ? Font.Bold : kind === "headline" ? Font.DemiBold : Font.Normal
    font.italic: kind === "ink"
    wrapMode: Text.Wrap
}
