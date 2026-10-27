// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
#include "onlinemodel.h"
#include "gamesettings.h"
#include <QClipboard>
#include <QGuiApplication>
#include <QJsonArray>
#include <QSettings>
#include <algorithm>
#include <memory>

static const QString network = "Network";

OnlineModel::OnlineModel(QObject* parent) : QObject(parent) {
    QSettings s;
    username_ = s.value("grid0User").toString();
    server_ = s.value("grid0Server", Grid0::defaultServer).toString();
    if (!username_.isEmpty()) {
        QString password = Grid0Credentials::load(username_);
        if (!password.isNull()) {
            session_ = new Grid0Session(server_, username_, password, this);
            account_ = "signedIn";
        }
    }
    connect(&patches_, &Grid0Patches::changed, this, &OnlineModel::changed);
}

void OnlineModel::setSettings(GameSettings* settings) {
    settings_ = settings;
    emit changed();
}

bool OnlineModel::playsOnline() const { return signedIn() && settings_ && settings_->raw("grid0") == "1"; }

void OnlineModel::setPlaysOnline(bool on) {
    if (!settings_) return;
    settings_->setRaw("grid0", on ? "1" : "0", network);
    if (on) {
        settings_->setRaw("network", "1", network); // GRID0+ needs the game online
        settings_->setRaw("grid0_user", username_, network);
        settings_->setRaw("grid0_server", server_, network);
    }
    emit changed();
}

QString OnlineModel::passwordForGame() const { return playsOnline() ? Grid0Credentials::load(username_) : QString(); }

void OnlineModel::signIn(const QString& user, const QString& password, const QString& serverText) {
    QString username = user.trimmed(), server = serverText.trimmed().isEmpty() ? Grid0::defaultServer : serverText.trimmed();
    if (username.isEmpty() || password.isEmpty()) {
        error_ = "Enter your GRID0+ user name and password.";
        emit changed();
        return;
    }
    account_ = "signingIn";
    error_.clear();
    emit changed();
    auto* candidate = new Grid0Session(server, username, password, this);
    candidate->call("GET", "/emulator/v1/me", nullptr,
                    [this, candidate, username, password, server](const Grid0Response& r, const QString& why) {
                        QString problem = !why.isEmpty() ? why : !r.ok() ? r.message() : QString();
                        QString stored;
                        if (problem.isEmpty() && !Grid0Credentials::save(username, password, &stored)) problem = stored;
                        if (!problem.isEmpty()) {
                            candidate->deleteLater();
                            account_ = session_ ? "signedIn" : "signedOut";
                            error_ = problem;
                            emit changed();
                            return;
                        }
                        if (username_ != username && !username_.isEmpty()) Grid0Credentials::remove(username_);
                        username_ = username;
                        server_ = server;
                        QSettings s;
                        s.setValue("grid0User", username);
                        s.setValue("grid0Server", server);
                        if (session_) session_->deleteLater();
                        session_ = candidate;
                        refreshing_ = 0; // the old session's answers never come
                        me_ = grid0Player(r.json());
                        account_ = "signedIn";
                        setPlaysOnline(true);
                        refresh();
                    });
}

void OnlineModel::signOut() {
    setPlaysOnline(false);
    Grid0Credentials::remove(username_);
    if (session_) session_->deleteLater();
    session_ = nullptr;
    refreshing_ = 0;
    me_.clear();
    friends_.clear();
    incoming_.clear();
    outgoing_.clear();
    refreshed_ = QDateTime();
    error_.clear();
    account_ = "signedOut";
    emit changed();
}

/// The account, its friends and its requests, fetched together
void OnlineModel::refresh() {
    if (!session_ || refreshing_) return;
    refreshing_ = 3;
    auto failure = std::make_shared<QString>();
    auto landed = [this, failure](const QString& why) {
        if (!why.isEmpty() && failure->isEmpty()) *failure = why;
        if (--refreshing_) return;
        if (failure->isEmpty()) {
            refreshed_ = QDateTime::currentDateTime();
            error_.clear();
        } else {
            error_ = *failure;
        }
        emit changed();
    };
    session_->call("GET", "/emulator/v1/me", nullptr, [this, landed](const Grid0Response& r, const QString& why) {
        if (why.isEmpty() && r.ok()) me_ = grid0Player(r.json());
        landed(!why.isEmpty() ? why : r.ok() ? QString() : r.message());
    });
    session_->call("GET", "/emulator/v1/friends", nullptr, [this, landed](const Grid0Response& r, const QString& why) {
        if (why.isEmpty() && r.ok()) {
            QList<QVariantMap> list;
            for (const QJsonValue& v : r.json().value("friends").toArray()) {
                QVariantMap p = grid0Player(v.toObject());
                if (!p.value("id").toString().isEmpty()) list.append(p);
            }
            std::sort(list.begin(), list.end(), [](const QVariantMap& a, const QVariantMap& b) {
                if (a["online"] != b["online"]) return a["online"].toBool();
                if (a["favorite"] != b["favorite"]) return a["favorite"].toBool();
                return a["nickname"].toString().compare(b["nickname"].toString(), Qt::CaseInsensitive) < 0;
            });
            friends_.clear();
            for (const QVariantMap& p : list) friends_.append(p);
        }
        landed(!why.isEmpty() ? why : r.ok() ? QString() : r.message());
    });
    session_->call("GET", "/emulator/v1/friend-requests", nullptr, [this, landed](const Grid0Response& r, const QString& why) {
        if (why.isEmpty() && r.ok()) {
            auto read = [&r](const char* key) {
                QVariantList out;
                for (const QJsonValue& v : r.json().value(key).toArray()) {
                    QJsonObject e = v.toObject();
                    if (!e.value("id").isString()) continue;
                    out.append(QVariantMap{{"id", e.value("id").toString()}, {"other", grid0Player(e.value("other").toObject())}});
                }
                return out;
            };
            incoming_ = read("incoming");
            outgoing_ = read("outgoing");
        }
        landed(!why.isEmpty() ? why : r.ok() ? QString() : r.message());
    });
}

void OnlineModel::act(const QString& key, const QString& method, const QString& path, const QJsonObject* json,
                      const QString& done, std::function<void()> after) {
    if (!session_ || busy_.contains(key)) return;
    busy_.insert(key);
    emit changed();
    session_->call(method, path, json, [this, key, done, after](const Grid0Response& r, const QString& why) {
        busy_.remove(key);
        if (why.isEmpty() && r.ok()) {
            error_.clear();
            notice_ = done;
            if (after) after();
            refresh();
        } else {
            error_ = !why.isEmpty() ? why : r.message();
        }
        emit changed();
    });
}

/// A request to the friend code given (SW-1234-5678-9012, or its twelve digits)
void OnlineModel::sendRequest(const QString& code) {
    QString digits;
    for (QChar c : code)
        if (c.isDigit()) digits += c;
    if (digits.size() != 12) {
        error_ = "A friend code has twelve digits: SW-1234-5678-9012.";
        emit changed();
        return;
    }
    QString formatted = "SW-" + digits.mid(0, 4) + "-" + digits.mid(4, 4) + "-" + digits.mid(8, 4);
    QJsonObject json{{"friendCode", formatted}};
    act(formatted, "POST", "/emulator/v1/friend-requests", &json, "Friend request sent to " + formatted + ".",
        [this] { emit requestSent(); });
}
void OnlineModel::accept(const QString& id, const QString& nickname) {
    QJsonObject json;
    act(id, "POST", "/emulator/v1/friend-requests/" + id + "/accept", &json, "You and " + nickname + " are friends now.");
}
void OnlineModel::decline(const QString& id) {
    QJsonObject json;
    act(id, "POST", "/emulator/v1/friend-requests/" + id + "/deny", &json, QString());
}
void OnlineModel::cancel(const QString& id) {
    QJsonObject json;
    act(id, "POST", "/emulator/v1/friend-requests/" + id + "/cancel", &json, QString());
}
void OnlineModel::remove(const QString& id, const QString& nickname) {
    act(id, "DELETE", "/emulator/v1/friends/" + id, nullptr, nickname + " was removed from your friends.");
}
void OnlineModel::dismissNotice() {
    notice_.clear();
    emit changed();
}
void OnlineModel::copy(const QString& text) { QGuiApplication::clipboard()->setText(text); }
