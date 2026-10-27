// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import QtQuick
import QtQuick.Dialogs

Window {
    width: 600
    height: 400
    visible: true
    property int phase: 0
    FileDialog { id: files; options: FileDialog.DontUseNativeDialog }
    FolderDialog { id: folders; options: FolderDialog.DontUseNativeDialog }
    Timer {
        interval: 500
        repeat: true
        running: true
        onTriggered: {
            if (phase === 0) files.open()
            else if (phase === 1) {
                if (!files.visible) { console.error("file picker did not open"); Qt.exit(1); return }
                files.close()
                folders.open()
            } else {
                if (!folders.visible) { console.error("folder picker did not open"); Qt.exit(1); return }
                folders.close()
                console.log("file and folder pickers opened")
                Qt.quit()
            }
            phase++
        }
    }
}
