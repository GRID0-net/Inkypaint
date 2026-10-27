// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
#pragma once
#include "game.h"
#include "gamesettings.h"
#include <QObject>
#include <QVariantList>

/// The Mods page: the game's mods folder (<id>_data/mods), each mod on or off (mods_off= in the game's .ini), and
/// adding one from a folder or an archive. A mod's romfs/ is laid over the game's files when it starts, so it needs
/// nothing more; its exefs/ is code, which takes a recompile. GRID0+'s own patches are left to the Online page
class ModsModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList mods READ mods NOTIFY changed) // {name, on, romfs, code}
    Q_PROPERTY(bool needsRecompile READ needsRecompile NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString notice READ notice NOTIFY changed)
public:
    ModsModel(const GamePaths* paths, GameSettings* settings, QObject* parent = nullptr);
    QVariantList mods() const { return mods_; }
    bool needsRecompile() const { return needsRecompile_; }
    bool busy() const { return busy_; }
    QString error() const { return error_; }
    QString notice() const { return notice_; }
    QString folder() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setOn(const QString& name, bool on);
    Q_INVOKABLE void remove(const QString& name);
    Q_INVOKABLE void add(const QString& source); // a folder or an archive (.zip, .7z, .rar), a path or a file: URL
    Q_INVOKABLE void reveal();
    Q_INVOKABLE void dismiss();

signals:
    void changed();

private:
    void finished(const QStringList& added, const QString& error);
    const GamePaths* paths_;
    GameSettings* settings_;
    QVariantList mods_;
    bool needsRecompile_ = false, busy_ = false;
    QString error_, notice_;
};
