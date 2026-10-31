// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
#include "discordipc.h"
#include "presencescene.h"
#include <QCoreApplication>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#define CHECK(x) do { if (!(x)) { qCritical() << __LINE__ << #x; return 1; } } while (0)
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    // frames: little-endian opcode and length, split arrivals wait for the rest
    QByteArray payload = DiscordIpc::handshake("1234567890");
    QByteArray frame = DiscordIpc::encode(DiscordIpc::Handshake, payload);
    CHECK(frame.size() == payload.size() + 8);
    CHECK(frame[0] == 0 && quint8(frame[4]) == quint8(payload.size()));
    QByteArray stream = frame.left(5), out;
    quint32 op = 99;
    CHECK(DiscordIpc::take(stream, op, out) == 0);
    stream += frame.mid(5) + DiscordIpc::encode(DiscordIpc::Ping, "{}");
    CHECK(DiscordIpc::take(stream, op, out) == 1 && op == DiscordIpc::Handshake && out == payload);
    CHECK(DiscordIpc::take(stream, op, out) == 1 && op == DiscordIpc::Ping && out == "{}" && stream.isEmpty());
    QByteArray broken = DiscordIpc::encode(DiscordIpc::Frame, QByteArray());
    broken[6] = 0x7f;
    CHECK(DiscordIpc::take(broken, op, out) == -1);
    CHECK(QJsonDocument::fromJson(payload).object().value("client_id").toString() == "1234567890");

    // the activity
    DiscordIpc::Activity a{"In a battle", "Splat Zones on Mahi-Mahi Resort", "Splatoon 3 on inkypaint", "battle", "Battle", 1793404800};
    QJsonObject set = QJsonDocument::fromJson(DiscordIpc::setActivity(42, &a, "n1")).object();
    CHECK(set.value("cmd").toString() == "SET_ACTIVITY" && set.value("nonce").toString() == "n1");
    QJsonObject args = set.value("args").toObject(), act = args.value("activity").toObject();
    CHECK(args.value("pid").toInt() == 42);
    CHECK(act.value("details").toString() == "In a battle" && act.value("state").toString() == a.state);
    CHECK(qint64(act.value("timestamps").toObject().value("start").toDouble()) == 1793404800);
    CHECK(act.value("assets").toObject().value("large_image").toString() == "inkypaint");
    CHECK(act.value("assets").toObject().value("small_image").toString() == "battle");
    CHECK(act.value("type").toInt(-1) == 0);
    DiscordIpc::Activity bare{"In Splatsville", "", "", "", "", 0};
    QJsonObject bareAct = QJsonDocument::fromJson(DiscordIpc::setActivity(1, &bare, "n2")).object()
                              .value("args").toObject().value("activity").toObject();
    CHECK(!bareAct.contains("state") && !bareAct.contains("timestamps")); // too short for Discord: left out
    CHECK(!QJsonDocument::fromJson(DiscordIpc::setActivity(1, nullptr, "n3")).object().value("args").toObject().contains("activity"));

    // the scenes
    Presence::SceneText t = Presence::describeScene("Plaza", 0, -1, -1, -1);
    CHECK(t.details == "In Splatsville" && t.smallImage == "plaza");
    CHECK(Presence::describeScene("Plaza_SAND", 0, -1, -1, -1).details == "At the Grand Festival Grounds");
    CHECK(Presence::describeScene("LobbyVersus", 0, -1, -1, -1).details == "In the battle lobby");
    t = Presence::describeScene("Vss_Hiagari04", 0, 1, 0, 3);
    CHECK(t.details == "In a battle" && t.state == "Splat Zones on Mahi-Mahi Resort" && t.smallImage == "battle");
    CHECK(Presence::describeScene("Vss_Yagara", 0, 9, 0, 0).state == "Battle on Hagglefish Market");
    CHECK(Presence::describeScene("Vss_Newstage00", 0, 0, 0, 0).state == "Turf War on Newstage");
    t = Presence::describeScene("Cop_Shakeup", 0, -1, -1, -1);
    CHECK(t.details == "Playing Salmon Run" && t.state == "Spawning Grounds" && t.smallImage == "salmon");
    CHECK(Presence::describeScene("Msn_A01", 0, -1, -1, -1).smallImage == "hero");
    CHECK(Presence::describeScene("SdodrLobby", 2, -1, -1, -1).smallImage == "sideorder");
    t = Presence::describeScene("Something", 0, -1, -1, -1);
    CHECK(t.details == "Playing Splatoon 3" && t.smallImage.isEmpty());

    // the runtime's status: fresh and the game's, or unknown
    QByteArray status = R"({"version":1,"title":"0100C2500FC20000","pid":77,"updated_ms":100000,"scene":"Cop_Shakeup","content":0,"rule":-1,"mode":-1,"stage_id":-1})";
    Presence::GameStatus g = Presence::parseStatus(status, 77, 110000);
    CHECK(g.known && g.scene == "Cop_Shakeup" && g.rule == -1 && g.content == 0);
    CHECK(!Presence::parseStatus(status, 77, 116000).known); // stale
    CHECK(!Presence::parseStatus(status, 78, 110000).known); // another process
    CHECK(!Presence::parseStatus("not json", 77, 110000).known);
    CHECK(!Presence::parseStatus(QByteArray(), 77, 110000).known); // missing

    // the launcher's states
    Presence::LauncherState s;
    s.pane = "mods";
    s.launcherStart = 1000;
    s.now = 1100;
    DiscordIpc::Activity l = Presence::describe(s);
    CHECK(l.details == "In the launcher" && l.state == "Managing mods" && l.start == 1000);
    s.phase = "recompiling";
    s.fraction = 0.63;
    CHECK(Presence::describe(s).state == "Recompiling the game (63%)");
    s.playing = true;
    s.gameStart = 1090;
    l = Presence::describe(s);
    CHECK(l.details == "Starting Splatoon 3" && l.start == 1090);
    s.game = g;
    l = Presence::describe(s);
    CHECK(l.details == "Playing Salmon Run" && l.largeText == "Splatoon 3 on inkypaint");
    qInfo("PASS launcher presence: frames, activity JSON, scenes, status freshness and launcher states");
    return 0;
}
