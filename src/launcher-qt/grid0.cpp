// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
#include "grid0.h"
#include "game.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QPointer>
#include <QSaveFile>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
#ifdef Q_OS_WIN
#include <windows.h>
#include <dpapi.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

static const char switchnetCA[] =
#include "switchnet_ca.inc"
    ;

bool Grid0::endpoint(const QString& server, QString* host, quint16* port) {
    QStringList parts = server.trimmed().split(':');
    if (parts.isEmpty() || parts.size() > 2 || parts[0].isEmpty()) return false;
    *host = parts[0];
    *port = 443;
    if (parts.size() == 2) {
        bool ok = false;
        uint p = parts[1].toUInt(&ok);
        if (!ok || p == 0 || p > 65535) return false;
        *port = quint16(p);
    }
    return true;
}

QJsonObject Grid0Response::json() const { return QJsonDocument::fromJson(body).object(); }
QString Grid0Response::message() const {
    QJsonObject j = json();
    for (const char* k : {"message", "error", "detail", "title"})
        if (j.value(k).isString()) return j.value(k).toString();
    return QString("The server answered %1.").arg(status);
}

/// The status line, the headers and the body (a chunked one put together)
static bool parseResponse(const QByteArray& raw, Grid0Response* out) {
    int split = raw.indexOf("\r\n\r\n");
    if (split < 0) return false;
    QList<QByteArray> lines = raw.left(split).split('\n');
    QList<QByteArray> status = lines.value(0).trimmed().split(' ');
    bool ok = false;
    out->status = status.value(1).toInt(&ok);
    if (!ok) return false;
    QByteArray body = raw.mid(split + 4);
    bool chunked = false;
    int length = -1;
    for (int i = 1; i < lines.size(); i++) {
        QByteArray l = lines[i].trimmed().toLower();
        if (l.startsWith("transfer-encoding:") && l.contains("chunked")) chunked = true;
        if (l.startsWith("content-length:")) length = l.mid(15).trimmed().toInt();
    }
    if (chunked) {
        QByteArray joined;
        int at = 0;
        for (;;) {
            int end = body.indexOf("\r\n", at);
            if (end < 0) break;
            int size = body.mid(at, end - at).split(';').value(0).trimmed().toInt(&ok, 16);
            if (!ok || size <= 0 || end + 2 + size > body.size()) break;
            joined += body.mid(end + 2, size);
            at = end + 2 + size + 2;
        }
        body = joined;
    } else if (length >= 0 && length < body.size()) {
        body.truncate(length);
    }
    out->body = body;
    return true;
}

void grid0Request(const QString& server, const QString& host, const QString& method, const QString& path,
                  const QMap<QByteArray, QByteArray>& headers, const QByteArray& body,
                  std::function<void(const Grid0Response&, const QString&)> done) {
    QString address;
    quint16 port = 0;
    if (!Grid0::endpoint(server, &address, &port)) {
        done({}, QString("\"%1\" isn't a server address. Use host or host:port.").arg(server));
        return;
    }
    // OpenSSL's backend where Qt has it (the package ships it): the same checks on every system
    static const bool backend = [] {
        if (QSslSocket::availableBackends().contains("openssl")) QSslSocket::setActiveBackend("openssl");
        return true;
    }();
    (void)backend;
    auto* socket = new QSslSocket;
    QSslConfiguration config = QSslConfiguration::defaultConfiguration();
    config.setCaCertificates(QSslCertificate::fromData(QByteArray(switchnetCA), QSsl::Pem));
    config.setPeerVerifyMode(QSslSocket::VerifyPeer);
    config.setProtocol(QSsl::TlsV1_2OrLater);
    config.setAllowedNextProtocols({"http/1.1"});
    socket->setSslConfiguration(config);
    socket->setPeerVerifyName(host); // the name checked against the certificate, and the SNI sent

    QByteArray request = method.toUtf8() + ' ' + path.toUtf8() + " HTTP/1.1\r\nHost: " + host.toUtf8() +
                         "\r\nUser-Agent: inkypaint\r\nAccept: application/json\r\nConnection: close\r\n"
                         "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    for (auto it = headers.cbegin(); it != headers.cend(); ++it) request += it.key() + ": " + it.value() + "\r\n";
    request += "\r\n" + body;

    auto received = std::make_shared<QByteArray>();
    auto finished = std::make_shared<bool>(false);
    auto finish = [socket, received, finished, done](const QString& error) {
        if (*finished) return;
        *finished = true;
        Grid0Response r;
        if (error.isEmpty() && !parseResponse(*received, &r)) {
            done({}, "GRID0+ sent an answer inkypaint doesn't understand.");
        } else if (!error.isEmpty() && received->isEmpty()) {
            done({}, "Couldn't reach GRID0+: " + error);
        } else {
            done(r, QString());
        }
        socket->disconnect();
        socket->abort();
        socket->deleteLater();
    };
    QObject::connect(socket, &QSslSocket::encrypted, socket, [socket, request] { socket->write(request); });
    QObject::connect(socket, &QSslSocket::readyRead, socket, [socket, received] { *received += socket->readAll(); });
    QObject::connect(socket, &QSslSocket::disconnected, socket, [socket, received, finish] {
        *received += socket->readAll();
        finish(QString());
    });
    QObject::connect(socket, &QSslSocket::sslErrors, socket, [finish](const QList<QSslError>& errors) {
        finish(errors.isEmpty() ? QString("its certificate isn't GRID0+'s") : errors.first().errorString());
    });
    QObject::connect(socket, &QSslSocket::errorOccurred, socket, [socket, finish](QAbstractSocket::SocketError e) {
        if (e == QAbstractSocket::RemoteHostClosedError) return; // the end of the answer: disconnected() follows
        finish(socket->errorString());
    });
    QTimer::singleShot(15000, socket, [finish] { finish("the server didn't answer in time"); });
    socket->connectToHostEncrypted(address, port, host);
}

QVariantMap grid0Player(const QJsonObject& j) {
    QString login = j.value("login").toString(), nick = j.value("nickname").toString();
    QString state = j.value("state").toString(), app = j.value("appId").toString();
    QString activity = state == "PLAYING"
                           ? (app.compare(Game::titleID, Qt::CaseInsensitive) == 0 ? "Playing " + Game::name : "Playing a game")
                       : state == "ONLINE"  ? "Online"
                       : state == "OFFLINE" ? "Offline"
                                            : "Status hidden";
    return {{"id", j.value("id").toString()},
            {"nickname", nick.isEmpty() ? login : nick},
            {"login", login},
            {"friendCode", j.value("friendCode").toString()},
            {"state", state},
            {"playing", state == "PLAYING"},
            {"online", state == "ONLINE" || state == "PLAYING"},
            {"favorite", j.value("isFavorite").toBool()},
            {"activity", activity}};
}

Grid0Session::Grid0Session(const QString& server, const QString& username, const QString& password, QObject* parent)
    : QObject(parent), server_(server), username_(username), password_(password) {}

void Grid0Session::signIn(std::function<void(const QString&)> done) {
    QByteArray form = "username=" + QUrl::toPercentEncoding(username_) + "&password=" + QUrl::toPercentEncoding(password_);
    grid0Request(server_, Grid0::accountHost, "POST", "/login",
                 {{"Content-Type", "application/x-www-form-urlencoded"}}, form,
                 [self = QPointer<Grid0Session>(this), done](const Grid0Response& r, const QString& error) {
                     if (!self) return; // signed out meanwhile
                     if (!error.isEmpty()) return done(error);
                     if (!r.ok()) return done(r.status == 401 ? "Wrong user name or password." : r.message());
                     QString token = r.json().value("accessToken").toString();
                     if (token.isEmpty()) return done("GRID0+ sent an answer inkypaint doesn't understand.");
                     self->access_ = token;
                     done(QString());
                 });
}

void Grid0Session::call(const QString& method, const QString& path, const QJsonObject* json, Done done) {
    QByteArray body = json ? QJsonDocument(*json).toJson(QJsonDocument::Compact) : QByteArray();
    attempt(method, path, body, json != nullptr, 0, done);
}

void Grid0Session::attempt(const QString& method, const QString& path, const QByteArray& body, bool hasBody, int n,
                           Done done) {
    QPointer<Grid0Session> self(this);
    auto send = [self, method, path, body, hasBody, n, done] {
        if (!self) return;
        QMap<QByteArray, QByteArray> headers{{"Authorization", "Bearer " + self->access_.toUtf8()}};
        if (hasBody) headers.insert("Content-Type", "application/json");
        grid0Request(self->server_, Grid0::accountHost, method, path, headers, body,
                     [self, method, path, body, hasBody, n, done](const Grid0Response& r, const QString& error) {
                         if (!self) return;
                         if (error.isEmpty() && r.status == 401 && n == 0) // expired: sign in again, once
                             return self->attempt(method, path, body, hasBody, 1, done);
                         done(r, error);
                     });
    };
    if (access_.isEmpty() || n == 1) {
        signIn([send, done](const QString& error) {
            if (!error.isEmpty()) return done({}, error);
            send();
        });
    } else {
        send();
    }
}

// ---- credentials
static QString credentialsFile() {
    return QDir(GamePaths::appData()).filePath(QStringLiteral("Credentials/grid0.json"));
}

bool Grid0Credentials::save(const QString& account, const QString& password, QString* error) {
    QString path = credentialsFile(), dir = QFileInfo(path).absolutePath();
    QDir().mkpath(dir);
    QByteArray secret = password.toUtf8();
#ifdef Q_OS_WIN
    DATA_BLOB in{DWORD(secret.size()), reinterpret_cast<BYTE*>(secret.data())}, out{};
    if (!CryptProtectData(&in, L"inkypaint GRID0+", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        if (error) *error = "Couldn't protect the password with Windows' data protection.";
        return false;
    }
    secret = QByteArray(reinterpret_cast<const char*>(out.pbData), int(out.cbData));
    LocalFree(out.pbData);
#else
    chmod(QFile::encodeName(dir).constData(), 0700);
#endif
    QJsonObject j{{"account", account}, {"password", QString::fromLatin1(secret.toBase64())}};
#ifdef Q_OS_WIN
    j.insert("protection", "dpapi");
#endif
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = "Couldn't create the local credentials file.";
        return false;
    }
#ifndef Q_OS_WIN
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
#endif
    f.write(QJsonDocument(j).toJson(QJsonDocument::Compact));
    if (!f.commit()) {
        if (error) *error = "Couldn't write the local credentials file.";
        return false;
    }
    return true;
}

QString Grid0Credentials::load(const QString& account) {
    QFile f(credentialsFile());
    if (f.size() > 65536 || !f.open(QIODevice::ReadOnly)) return QString();
#ifndef Q_OS_WIN
    struct stat st;
    if (fstat(f.handle(), &st) != 0 || st.st_uid != getuid() || (st.st_mode & 077)) return QString();
#endif
    QJsonObject j = QJsonDocument::fromJson(f.readAll()).object();
    if (j.value("account").toString() != account) return QString();
    QByteArray secret = QByteArray::fromBase64(j.value("password").toString().toLatin1());
#ifdef Q_OS_WIN
    if (j.value("protection").toString() != "dpapi") return QString();
    DATA_BLOB in{DWORD(secret.size()), reinterpret_cast<BYTE*>(secret.data())}, out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return QString();
    secret = QByteArray(reinterpret_cast<const char*>(out.pbData), int(out.cbData));
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
#endif
    return QString::fromUtf8(secret);
}

void Grid0Credentials::remove(const QString& account) {
    QFile f(credentialsFile());
    if (f.open(QIODevice::ReadOnly) && QJsonDocument::fromJson(f.read(65536)).object().value("account") == account) {
        f.close();
        f.remove();
    }
}
