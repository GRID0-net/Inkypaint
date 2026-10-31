// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
#include "presencescene.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <iterator>

namespace Presence {
namespace {
struct Name {
    const char* label;
    const char* name;
};
// the labels in the game's scene names (Vss_Hiagari04, Cop_Shakeup) and their stages' English names
const Name battleStages[] = {
    {"AutoWalk", "Lemuria Hub"},           {"BigSlope", "Brinewater Springs"},
    {"Carousel", "Wahoo World"},           {"Crank", "Urchin Underpass"},
    {"Cross", "Robo ROM-en"},              {"District", "Eeltail Alley"},
    {"Factory", "Um'ami Ruins"},           {"Hiagari", "Mahi-Mahi Resort"},
    {"Jyoheki", "Flounder Heights"},       {"Kaisou", "Hammerhead Bridge"},
    {"Line", "MakoMart"},                  {"Manbou", "Grand Splatlands Bowl"},
    {"Nagasaki", "Sturgeon Shipyard"},     {"Pillar", "Manta Maria"},
    {"Pivot", "Museum d'Alfonsino"},       {"Propeller", "Marlin Airport"},
    {"Ruins", "Bluefin Depot"},            {"Scrap", "Mincemeat Metalworks"},
    {"Section", "Barnacle & Dime"},        {"Spider", "Crableg Capital"},
    {"Temple", "Undertow Spillway"},       {"Twist", "Shipshape Cargo Co."},
    {"Upland", "Inkblot Art Academy"},     {"Wave", "Humpback Pump Track"},
    {"Yagara", "Hagglefish Market"},       {"Yunohana", "Scorch Gorge"},
};
const Name salmonSites[] = {
    {"Carousel", "Wahoo World"},             {"District", "Eeltail Alley"},
    {"Factory", "Um'ami Ruins"},             {"Manbou", "Grand Splatlands Bowl"},
    {"Section", "Barnacle & Dime"},          {"Shakedent", "Gone Fission Hydroplant"},
    {"Shakehighway", "Jammin' Salmon Junction"}, {"Shakelift", "Salmonid Smokeyard"},
    {"Shakerail", "Bonerattle Arena"},       {"Shakeship", "Marooner's Bay"},
    {"Shakespiral", "Sockeye Station"},      {"Shakeup", "Spawning Grounds"},
    {"Temple", "Undertow Spillway"},         {"Tutorial", "Sockeye Station"},
    {"Upland", "Inkblot Art Academy"},
};

// Vss_Hiagari04 is Hiagari: the prefix and the trailing digits go
QString labelOf(const QString& scene) {
    QString label = scene.mid(scene.indexOf('_') + 1);
    while (!label.isEmpty() && label.back().isDigit()) label.chop(1);
    return label;
}
}

GameStatus parseStatus(const QByteArray& json, qint64 pid, qint64 nowMs) {
    GameStatus s;
    QJsonParseError error;
    QJsonObject o = QJsonDocument::fromJson(json, &error).object();
    if (error.error != QJsonParseError::NoError || o.value("version").toInt() != 1) return s;
    if (pid > 0 && qint64(o.value("pid").toDouble()) != pid) return s;
    qint64 updated = qint64(o.value("updated_ms").toDouble());
    if (updated <= 0 || nowMs - updated > 15000) return s;
    s.known = true;
    s.scene = o.value("scene").toString();
    s.content = o.value("content").toInt(-1);
    s.rule = o.value("rule").toInt(-1);
    s.mode = o.value("mode").toInt(-1);
    s.stageId = o.value("stage_id").toInt(-1);
    return s;
}

QString stageName(const QString& label, bool salmonRun) {
    const Name* table = salmonRun ? salmonSites : battleStages;
    size_t n = salmonRun ? std::size(salmonSites) : std::size(battleStages);
    for (size_t i = 0; i < n; i++)
        if (label == QLatin1String(table[i].label)) return QString::fromUtf8(table[i].name);
    return label;
}

QString ruleName(int rule) {
    static const char* const names[] = {"Turf War", "Splat Zones", "Tower Control", "Rainmaker", "Clam Blitz",
                                        "Tricolor Turf War"};
    return rule >= 0 && rule < int(std::size(names)) ? QString::fromLatin1(names[rule]) : QString();
}

SceneText describeScene(const QString& scene, int content, int rule, int mode, int stageId) {
    (void)content; // the DLC's Splatsville is still Splatsville
    (void)mode;
    (void)stageId; // the stage is in the scene's name
    auto starts = [&](const char* prefix) { return scene.startsWith(QLatin1String(prefix)); };
    if (scene == "Boot") return {"Starting Splatoon 3", "Loading the game", {}, {}};
    if (scene == "Plaza_SAND") return {"At the Grand Festival Grounds", {}, "plaza", "Splatsville"};
    if (starts("Plaza")) return {"In Splatsville", {}, "plaza", "Splatsville"};
    if (scene == "LobbyVersus") return {"In the battle lobby", {}, "battle", "Battle"};
    if (scene == "LobbyLocal") return {"In the local lobby", {}, "battle", "Battle"};
    if (scene == "LobbyCoop") return {"In the Grizzco lobby", {}, "salmon", "Salmon Run"};
    if (starts("Vss_")) {
        QString rules = ruleName(rule), stage = stageName(labelOf(scene), false);
        return {"In a battle", (rules.isEmpty() ? QStringLiteral("Battle") : rules) + " on " + stage, "battle",
                "Battle"};
    }
    if (starts("Cop_")) return {"Playing Salmon Run", stageName(labelOf(scene), true), "salmon", "Salmon Run"};
    if (starts("Msn_") || starts("LastBoss") || scene == "BigWorld" || scene == "SmallWorld" ||
        scene == "LaunchPadWorld")
        return {"Playing Hero Mode", {}, "hero", "Hero Mode"};
    if (starts("Sdodr")) return {"Playing Side Order", {}, "sideorder", "Side Order"};
    if (starts("Shop")) return {"Shopping in Splatsville", {}, "plaza", "Splatsville"};
    if (scene == "ShootingRange") return {"At the shooting range", {}, {}, {}};
    if (scene == "StaffRoll") return {"Watching the credits", {}, {}, {}};
    return {"Playing Splatoon 3", {}, {}, {}};
}

DiscordIpc::Activity describe(const LauncherState& s) {
    DiscordIpc::Activity a;
    if (s.playing) {
        a.start = s.gameStart;
        a.largeText = QStringLiteral("Inkypaint");
        if (s.game.known) {
            SceneText t = describeScene(s.game.scene, s.game.content, s.game.rule, s.game.mode, s.game.stageId);
            a.details = t.details;
            a.state = t.state;
            a.smallImage = t.smallImage;
            a.smallText = t.smallText;
        } else if (s.now - s.gameStart < 120) {
            a.details = QStringLiteral("Starting Splatoon 3");
            a.state = QStringLiteral("Loading the game");
        } else { // no status from the runtime (its discord_presence_status off, or an older runtime)
            a.details = QStringLiteral("Playing Splatoon 3");
        }
        if (s.online) { // the second line has room: its own words when empty, else added to the scene's
            a.state = a.state.isEmpty() ? QStringLiteral("Online with GRID0+")
                                        : a.state + QStringLiteral(" \u00b7 GRID0+");
        }
        return a;
    }
    a.start = s.launcherStart;
    a.largeText = QStringLiteral("Inkypaint");
    if (s.phase == "extracting" || s.phase == "recompiling" || s.phase == "installingDlc") {
        a.details = QStringLiteral("Setting up the game");
        if (s.phase == "extracting") a.state = QStringLiteral("Extracting the game");
        else if (s.phase == "installingDlc") a.state = QStringLiteral("Installing the DLC");
        else a.state = QStringLiteral("Recompiling the game (%1%)").arg(qRound(qBound(0.0, s.fraction, 1.0) * 100));
        return a;
    }
    a.details = QStringLiteral("In the launcher");
    if (s.pane == "online") a.state = QStringLiteral("Online with GRID0+");
    else if (s.pane == "settings" || s.pane == "keyboardInput" || s.pane == "controllerInput")
        a.state = QStringLiteral("Changing settings");
    else if (s.pane == "files") a.state = QStringLiteral("Setting up the game");
    else if (s.pane == "mods") a.state = QStringLiteral("Managing mods");
    else a.state = QStringLiteral("Ready to play");
    return a;
}
}
