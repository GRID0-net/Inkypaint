// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QString>
#include <functional>

/// GRID0+, the community's replacement for the console's servers (the Mac launcher's Grid0.swift). The launcher signs
/// in there and manages the account's friends; the game does the same through the runtime
/// (src/nx/hle/services/network/grid0.c), with the password the launcher hands it when it starts.
///
/// The server is dialled at its address and named by the console's host it stands in for: SwitchNet's edge routes on
/// the TLS server name and the Host header. The server is trusted only when its certificate chains to the SwitchNet
/// Local CA (no other root counts) and names that host.
namespace Grid0 {
inline const QString defaultServer = QStringLiteral("89.168.58.206");
inline const QString accountHost = QStringLiteral("e0d67c509fb203858ebcb2fe3f88c2aa.baas.nintendo.com");
inline const QString gameBuild = QStringLiteral("28C4287AEE36F7499DA60F3E68B54C70DA382D75"); // 11.3.0's build ID
/// "host" or "host:port" (443 without one); false for anything else
bool endpoint(const QString& server, QString* host, quint16* port);
}

struct Grid0Response {
    int status = 0;
    QByteArray body;
    bool ok() const { return status >= 200 && status < 300; }
    QJsonObject json() const;
    QString message() const; // what the server says went wrong, for the player
};

/// One HTTP/1.1 request on a connection of its own (Connection: close). done gets the response, or an error text
void grid0Request(const QString& server, const QString& host, const QString& method, const QString& path,
                  const QMap<QByteArray, QByteArray>& headers, const QByteArray& body,
                  std::function<void(const Grid0Response&, const QString& error)> done);

/// A GRID0+ player: the account signed in, a friend, or the other side of a request
QVariantMap grid0Player(const QJsonObject& json);

/// The account's session: its sign-in and the emulator API (/emulator/v1/...) with the access token it gave
class Grid0Session : public QObject {
    Q_OBJECT
public:
    Grid0Session(const QString& server, const QString& username, const QString& password, QObject* parent = nullptr);
    using Done = std::function<void(const Grid0Response&, const QString& error)>;
    /// POST /login with the user name and password, form encoded: the access token for the API
    void signIn(std::function<void(const QString& error)> done);
    /// A request to the API, signing in first, and again once when the token has expired
    void call(const QString& method, const QString& path, const QJsonObject* json, Done done);
    QString server() const { return server_; }
    QString username() const { return username_; }

private:
    void attempt(const QString& method, const QString& path, const QByteArray& body, bool hasBody, int n, Done done);
    QString server_, username_, password_, access_;
};

/// The password GRID0+ signs in with, kept for the game: protected with DPAPI on Windows (for this Windows user alone),
/// an owner-only file elsewhere, in the app's data folder
namespace Grid0Credentials {
bool save(const QString& account, const QString& password, QString* error);
QString load(const QString& account);
void remove(const QString& account);
}
