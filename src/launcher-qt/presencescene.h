// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
#pragma once
#include "discordipc.h"
#include <QByteArray>
#include <QString>
#include <optional>

/// What the rich presence says, decided from the launcher's state and the runtime's <id>.presence.json. Pure (QtCore),
/// so tests/launcher_presence.cpp checks it. presencemodel.cpp gathers the state
namespace Presence {
/// The runtime's status file (src/nx/runtime/inkypaint_presence.c), known only when it is fresh and the game's
struct GameStatus {
    bool known = false;
    QString scene;
    int content = -1, rule = -1, mode = -1, stageId = -1;
};
/// Unknown when json does not parse, names another pid than pid (when pid is above 0), or was updated more than
/// 15 seconds before nowMs
GameStatus parseStatus(const QByteArray& json, qint64 pid, qint64 nowMs);

struct SceneText {
    QString details, state, smallImage, smallText;
};
/// The scene's words: Plaza is Splatsville, Vss_Hiagari04 with rule 1 is Splat Zones on Mahi-Mahi Resort
SceneText describeScene(const QString& scene, int content, int rule, int mode, int stageId);
/// A battle stage's or Salmon Run site's English name from a scene's label (Hiagari, Shakeup), or the label itself
QString stageName(const QString& label, bool salmonRun);
/// The rule's name by the runtime's number (a guess: 0 Turf War to 5 Tricolor Turf War), empty when unknown
QString ruleName(int rule);

struct LauncherState {
    QString pane;              // play, online, settings, keyboardInput, controllerInput, mods, files
    QString phase;             // the Preparer's: idle, extracting, recompiling, installingDlc, done, failed
    double fraction = 0;       // the preparation's, 0 to 1
    bool playing = false;      // the game's process is running
    GameStatus game;
    qint64 launcherStart = 0;  // seconds since 1970
    qint64 gameStart = 0;
    qint64 now = 0;
};
/// The activity to show
DiscordIpc::Activity describe(const LauncherState& s);
}
