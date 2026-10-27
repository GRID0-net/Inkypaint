// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
#pragma once
#include <QProcessEnvironment>
#include <QString>

/// The one game inkypaint runs, and the build of it.
namespace Game {
inline const QString name = QStringLiteral("Splatoon 3");
inline const QString titleID = QStringLiteral("0100C2500FC20000");
inline const QString updateID = QStringLiteral("0100C2500FC20800");
inline const QString version = QStringLiteral("11.3.0");
}

/// Where a game's files live: ScarletNX's data folder (its --data), with the extraction in work/ and the recompiled
/// game, its settings and its log in recompiled/<title ID>/. As the Mac launcher's GamePaths (src/launcher)
struct GamePaths {
    QString data;

    static QString appData();     // %LOCALAPPDATA%\inkypaint, ~/.local/share/inkypaint
    static QString defaultData(); // appData()/data
    static QString logs();        // appData()/logs

    QString work() const;
    QString recompiled() const;
    QString gameDir() const;
    QString executable() const; // the game's copy of the runtime, named after it (.exe on Windows)
    QString settings() const;
    QString gameLog() const;

    bool isExtracted() const; // exefs/main and romfs/ are both there
    bool isPrepared() const;  // the runtime's copy is deployed last, so it marks a whole recompile
};

/// What the launcher carries beside itself (scripts/package-windows.sh puts it there): the runtime, runtime/scarletnx(.exe).
/// INKYPAINT_RUNTIME points at a build instead, for a run from the build folder
namespace Bundled {
QString runtime();
bool isComplete();
QProcessEnvironment environment(bool grid0Stdin = false);
}

/// Bytes free on the volume of path (or of its nearest existing parent), -1 when unknown
qint64 freeSpace(const QString& path);
/// A path for people: the home folder as ~ (Linux), native separators (Windows)
QString displayPath(const QString& path);
/// Shows path in the file manager (Explorer selects it)
void revealInFileManager(const QString& path);
