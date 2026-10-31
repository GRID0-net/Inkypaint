// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
#pragma once
#include "game.h"
#include "gamesettings.h"
#include "modsmodel.h"
#include "onlinemodel.h"
#include "preparer.h"
#include <QDateTime>
#include <QFile>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QUrl>

/// The launcher's state: where the game is, whether it is ready, and the game while it runs (the Mac's AppModel)
class AppModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString pane READ pane WRITE setPane NOTIFY paneChanged) // play, online, settings, files
    Q_PROPERTY(bool prepared READ prepared NOTIFY changed)
    Q_PROPERTY(bool extracted READ extracted NOTIFY changed)
    Q_PROPERTY(bool playing READ playing NOTIFY changed)
    Q_PROPERTY(QString statusLine READ statusLine NOTIFY changed)
    Q_PROPERTY(QString lastExit READ lastExit NOTIFY changed)
    Q_PROPERTY(QString dataFolder READ dataFolder NOTIFY changed)
    Q_PROPERTY(QString spaceLine READ spaceLine NOTIFY changed)
    Q_PROPERTY(bool hideWhilePlaying READ hideWhilePlaying WRITE setHideWhilePlaying NOTIFY changed)
    Q_PROPERTY(bool discordPresence READ discordPresence WRITE setDiscordPresence NOTIFY changed)
    Q_PROPERTY(QUrl logo READ logo CONSTANT)
    Q_PROPERTY(QString gameName READ gameName CONSTANT)
    Q_PROPERTY(QString gameVersion READ gameVersion CONSTANT)
    Q_PROPERTY(GameSettings* settings READ settings CONSTANT)
    Q_PROPERTY(Preparer* preparer READ preparer CONSTANT)
    Q_PROPERTY(GameSources* sources READ sources CONSTANT)
    Q_PROPERTY(OnlineModel* online READ online CONSTANT)
    Q_PROPERTY(ModsModel* mods READ mods CONSTANT)
    Q_PROPERTY(QString setupStep READ setupStep NOTIFY changed) // idle, askingOnline, downloadingPatches
public:
    explicit AppModel(QObject* parent = nullptr);
    QString pane() const { return pane_; }
    void setPane(const QString& pane);
    bool prepared() const { return paths_.isPrepared(); }
    bool extracted() const { return paths_.isExtracted(); }
    bool playing() const { return game_ != nullptr; }
    QString statusLine() const;
    QString lastExit() const { return lastExit_; }
    QString dataFolder() const { return displayPath(paths_.data); }
    QString spaceLine() const;
    bool hideWhilePlaying() const { return hide_; }
    void setHideWhilePlaying(bool on);
    bool discordPresence() const { return discord_; }
    void setDiscordPresence(bool on);
    qint64 gamePid() const { return game_ ? game_->processId() : 0; }
    QDateTime gameStarted() const { return started_; }
    bool gameOnline() const { return gameOnline_; } // this run was started with the GRID0+ sign-in
    // the runtime's status for the rich presence, beside its log (src/nx/runtime/inkypaint_presence.c)
    QString presenceFile() const;
    // made by the runtime when Escape is pressed in the game (src/nx/runtime/inkypaint_launcher.c), beside presenceFile()
    QString quitRequestFile() const;
    QUrl logo() const;
    QString gameName() const { return Game::name; }
    QString gameVersion() const { return Game::version; }
    GameSettings* settings() { return &settings_; }
    Preparer* preparer() { return &preparer_; }
    GameSources* sources() { return &sources_; }
    OnlineModel* online() { return &online_; }
    ModsModel* mods() { return &mods_; }
    QString setupStep() const { return setupStep_; }

    Q_INVOKABLE void play();
    Q_INVOKABLE void stop();
    // the answer to quitRequested: true closes the game, false lets it play on (hidden again when hide while playing)
    Q_INVOKABLE void answerQuit(bool close);
    Q_INVOKABLE void prepare();
    Q_INVOKABLE void installDlc(const QString& file);
    Q_INVOKABLE void recompile(); // for the GRID0+ patches: the extraction is kept, so it is the recompile alone
    // Setting up asks about online play first: signed in, GRID0+'s patches are downloaded before the recompile, so they
    // are compiled in the first time instead of needing a second recompile after signing in
    Q_INVOKABLE void requestPrepare(); // signed in: patches, then prepare. Otherwise asks (setupStep askingOnline)
    Q_INVOKABLE void prepareOffline(); // the question answered no: prepare without the patches
    Q_INVOKABLE void cancelSetup();
    Q_INVOKABLE void prepareWithPatches(); // signed in: the patches downloaded, then prepare
    Q_INVOKABLE void useDataFolder(const QString& folder); // a path or a file: URL
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void revealGameFolder() { revealInFileManager(paths_.gameDir()); }
    Q_INVOKABLE void revealGameLog() { revealInFileManager(paths_.gameLog()); }
    Q_INVOKABLE void updatePatches() { online_.patches()->update(paths_); }
    Q_INVOKABLE bool patchesNeedRecompile() { return online_.patches()->needsRecompile(paths_); }

signals:
    void paneChanged();
    void changed();
    void minimizeRequested();
    void restoreRequested();
    void quitRequested(); // Escape in the game: ask whether to close it, then answerQuit

private:
    void ended(int status, QProcess::ExitStatus how);
    bool deployRuntime(QString* error);
    void pollQuitRequest();
    void setSetupStep(const QString& step);
    void startPrepare(); // prepare, from the start again if the last one ended
    QString pane_ = "play";
    QString setupStep_ = "idle";
    bool setupOnline_ = false; // this setup chose online: play online once it is done
    QMetaObject::Connection patchesWait_;
    GamePaths paths_;
    GameSettings settings_;
    Preparer preparer_;
    GameSources sources_;
    OnlineModel online_;
    ModsModel mods_{&paths_, &settings_};
    QProcess* game_ = nullptr;
    QFile* gameOutput_ = nullptr;
    QDateTime started_;
    QString lastExit_;
    bool hide_ = true;
    bool discord_ = true;
    bool gameOnline_ = false; // the running game got the GRID0+ password: it plays online
    bool stopping_ = false; // Quit was asked for: its end is no surprise
    bool askingQuit_ = false; // quitRequested is waiting for answerQuit: a second request does not ask again
    QTimer* quitPoll_ = nullptr;
};
