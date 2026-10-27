// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
#pragma once
#include "game.h"
#include <QNetworkAccessManager>
#include <QObject>

/// GRID0+'s patches to the game's code: IPS files for the 11.3.0 executable, from the newest patches-<serial> release
/// of GRID0-net/GRID0plus-cfw (the Mac launcher's Grid0Patches.swift). inkypaint compiles the game ahead of time, so a
/// patch is a mod (mods/grid0plus-<category>/exefs/<build ID>.ips in the game's data folder), and the game is
/// recompiled with it. Each file is checked against the release manifest's SHA-256 before anything is written
class Grid0Patches : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY changed) // unknown, checking, current, failed
    Q_PROPERTY(QString tag READ tag NOTIFY changed)
    Q_PROPERTY(QString failure READ failure NOTIFY changed)
public:
    explicit Grid0Patches(QObject* parent = nullptr);
    QString status() const { return status_; }
    QString tag() const { return tag_; }
    QString failure() const { return failure_; }

    static bool installed(const GamePaths& paths);
    static bool compiledIn(const GamePaths& paths); // the runtime's stamp says the game was recompiled with mods
    bool needsRecompile(const GamePaths& paths) const;
    void recompiled();
    void update(const GamePaths& paths);

signals:
    void changed();

private:
    void get(const QString& url, std::function<void(const QByteArray&, const QString& error)> done);
    void fail(const QString& why);
    void setChanged(bool on);
    QNetworkAccessManager net_;
    QString status_ = "unknown", tag_, failure_;
};
