// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
#pragma once
#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QtEndian>

/// Discord's local RPC, as its desktop client speaks it over discord-ipc-N: each frame a little-endian opcode and
/// length, then that many bytes of UTF-8 JSON. Pure, so a QtCore test checks it (tests/launcher_presence.cpp), the
/// socket is discordpresence.cpp's
namespace DiscordIpc {
enum Opcode : quint32 { Handshake = 0, Frame = 1, Close = 2, Ping = 3, Pong = 4 };
constexpr quint32 maxPayload = 64 * 1024; // Discord's own frames are small: more is a broken stream

/// What the player is shown: a field shorter than 2 characters is left out, as Discord refuses the whole update
struct Activity {
    QString details, state, largeText, smallImage, smallText;
    qint64 start = 0; // seconds since 1970, 0 for none
    bool operator==(const Activity& o) const {
        return details == o.details && state == o.state && largeText == o.largeText && smallImage == o.smallImage &&
               smallText == o.smallText && start == o.start;
    }
    bool operator!=(const Activity& o) const { return !(*this == o); }
};

inline QByteArray encode(quint32 opcode, const QByteArray& payload) {
    QByteArray frame(8, '\0');
    qToLittleEndian<quint32>(opcode, frame.data());
    qToLittleEndian<quint32>(quint32(payload.size()), frame.data() + 4);
    return frame + payload;
}

/// Takes the first whole frame off buffer: 1 with opcode and payload set, 0 when more bytes are needed, -1 when the
/// stream is broken (a length past maxPayload)
inline int take(QByteArray& buffer, quint32& opcode, QByteArray& payload) {
    if (buffer.size() < 8) return 0;
    quint32 length = qFromLittleEndian<quint32>(buffer.constData() + 4);
    if (length > maxPayload) return -1;
    if (quint32(buffer.size()) < 8 + length) return 0;
    opcode = qFromLittleEndian<quint32>(buffer.constData());
    payload = buffer.mid(8, int(length));
    buffer.remove(0, int(8 + length));
    return 1;
}

inline QByteArray handshake(const QString& clientId) {
    return QJsonDocument(QJsonObject{{"v", 1}, {"client_id", clientId}}).toJson(QJsonDocument::Compact);
}

inline void putText(QJsonObject& o, const char* key, const QString& text) {
    if (text.trimmed().size() >= 2) o.insert(QLatin1String(key), text.left(128));
}

inline QJsonObject activityObject(const Activity& a) {
    QJsonObject activity, assets;
    putText(activity, "details", a.details);
    putText(activity, "state", a.state);
    if (a.start > 0) activity.insert("timestamps", QJsonObject{{"start", a.start}});
    assets.insert("large_image", "inkypaint");
    putText(assets, "large_text", a.largeText);
    if (!a.smallImage.isEmpty()) {
        assets.insert("small_image", a.smallImage);
        putText(assets, "small_text", a.smallText);
    }
    activity.insert("assets", assets);
    activity.insert("type", 0);
    return activity;
}

/// SET_ACTIVITY for the process pid. Without an activity it clears what is shown
inline QByteArray setActivity(qint64 pid, const Activity* a, const QString& nonce) {
    QJsonObject args{{"pid", pid}};
    if (a) args.insert("activity", activityObject(*a));
    return QJsonDocument(QJsonObject{{"cmd", "SET_ACTIVITY"}, {"args", args}, {"nonce", nonce}})
        .toJson(QJsonDocument::Compact);
}
}
