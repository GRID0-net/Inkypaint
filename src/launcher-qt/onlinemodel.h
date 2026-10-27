// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
#pragma once
#include "grid0.h"
#include "grid0patches.h"
#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QVariantList>

class GameSettings;

/// Online play on GRID0+: the account (its user name in the launcher's settings, its password in Grid0Credentials),
/// its friends and requests, and the game's settings that turn it on ([Network]: network, grid0, grid0_server,
/// grid0_user). The game signs in itself, with the password the launcher hands it at start (the Mac's OnlineModel)
class OnlineModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString account READ account NOTIFY changed) // signedOut, signingIn, signedIn
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY changed)
    Q_PROPERTY(QVariantMap me READ me NOTIFY changed)
    Q_PROPERTY(QVariantList friends READ friends NOTIFY changed)
    Q_PROPERTY(QVariantList incoming READ incoming NOTIFY changed)
    Q_PROPERTY(QVariantList outgoing READ outgoing NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString notice READ notice NOTIFY changed)
    Q_PROPERTY(QString refreshed READ refreshed NOTIFY changed)
    Q_PROPERTY(QString username READ username NOTIFY changed)
    Q_PROPERTY(QString server READ server NOTIFY changed)
    Q_PROPERTY(bool playsOnline READ playsOnline NOTIFY changed)
    Q_PROPERTY(QStringList busy READ busy NOTIFY changed)
    Q_PROPERTY(Grid0Patches* patches READ patches CONSTANT)
    Q_PROPERTY(QString defaultServer READ defaultServer CONSTANT)
public:
    explicit OnlineModel(QObject* parent = nullptr);
    void setSettings(GameSettings* settings);

    QString account() const { return account_; }
    bool signedIn() const { return account_ == "signedIn"; }
    QVariantMap me() const { return me_; }
    QVariantList friends() const { return friends_; }
    QVariantList incoming() const { return incoming_; }
    QVariantList outgoing() const { return outgoing_; }
    QString error() const { return error_; }
    QString notice() const { return notice_; }
    QString refreshed() const { return refreshed_.isValid() ? refreshed_.toString("HH:mm") : QString(); }
    QString username() const { return username_; }
    QString server() const { return server_; }
    QStringList busy() const { return busy_.values(); }
    Grid0Patches* patches() { return &patches_; }
    QString defaultServer() const { return Grid0::defaultServer; }

    bool playsOnline() const;
    Q_INVOKABLE void setPlaysOnline(bool on);
    QString passwordForGame() const; // when the game plays online

    Q_INVOKABLE void signIn(const QString& username, const QString& password, const QString& server);
    Q_INVOKABLE void signOut();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void sendRequest(const QString& code);
    Q_INVOKABLE void accept(const QString& id, const QString& nickname);
    Q_INVOKABLE void decline(const QString& id);
    Q_INVOKABLE void cancel(const QString& id);
    Q_INVOKABLE void remove(const QString& id, const QString& nickname);
    Q_INVOKABLE void dismissNotice();
    Q_INVOKABLE void copy(const QString& text); // to the clipboard

signals:
    void changed();
    void requestSent(); // the add-friend field empties

private:
    void act(const QString& key, const QString& method, const QString& path, const QJsonObject* json,
             const QString& done, std::function<void()> after = {});
    QString account_ = "signedOut";
    QVariantMap me_;
    QVariantList friends_, incoming_, outgoing_;
    QString error_, notice_, username_, server_;
    QDateTime refreshed_;
    QSet<QString> busy_;
    QPointer<Grid0Session> session_;
    QPointer<GameSettings> settings_;
    Grid0Patches patches_;
    int refreshing_ = 0;
};
