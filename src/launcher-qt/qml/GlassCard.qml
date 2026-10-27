// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import QtQuick

// A rounded translucent panel with room inside (the Mac's GlassCard): content goes in its default property
Rectangle {
    id: root
    property color tint: "transparent"
    default property alias content: inner.data
    property real padding: 20
    radius: 22
    color: tint.a > 0 ? Qt.rgba(tint.r, tint.g, tint.b, 0.16) : Theme.glass
    border.color: tint.a > 0 ? Qt.rgba(tint.r, tint.g, tint.b, 0.4) : Theme.glassBorder
    border.width: 1
    implicitHeight: inner.childrenRect.height + 2 * padding
    Item {
        id: inner
        x: root.padding
        y: root.padding
        width: root.width - 2 * root.padding
        height: childrenRect.height
    }
}
