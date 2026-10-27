// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
#include "appmodel.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QSettings>
#include <QTimer>

AppModel::AppModel(QObject* parent) : QObject(parent) {
    QSettings s;
    paths_.data = s.value("dataFolder", GamePaths::defaultData()).toString();
    hide_ = s.value("hideWhilePlaying", true).toBool();
    settings_.setPath(paths_.settings());
    online_.setSettings(&settings_);
    if (!paths_.isPrepared()) pane_ = "files";
    connect(&preparer_, &Preparer::finished, this, [this] {
        refresh();
        mods_.refresh(); // a recompile takes the code mods in
        if (preparer_.phase() == "done") {
            if (Grid0Patches::compiledIn(paths_)) online_.patches()->recompiled();
            if (setupOnline_ && online_.signedIn()) online_.setPlaysOnline(true);
        }
        setupOnline_ = false;
    });
    // signing in from the question goes straight on to the patches and the setup
    connect(&online_, &OnlineModel::changed, this, [this] {
        if (setupStep_ == "askingOnline" && online_.signedIn()) prepareWithPatches();
    });
    connect(&preparer_, &Preparer::changed, this, &AppModel::changed);
    connect(&online_, &OnlineModel::changed, this, &AppModel::changed);
}

void AppModel::setPane(const QString& pane) {
    if (pane_ == pane) return;
    pane_ = pane;
    emit paneChanged();
}

QString AppModel::statusLine() const {
    if (playing()) return "Playing since " + QLocale().toString(started_.time(), QLocale::ShortFormat);
    if (!prepared()) return "Not set up yet";
    QDateTime last = QSettings().value("lastPlayed").toDateTime();
    if (!last.isValid()) return "Ready to play";
    qint64 days = last.date().daysTo(QDate::currentDate());
    return days == 0 ? "Last played today" : days == 1 ? "Last played yesterday" : QString("Last played %1 days ago").arg(days);
}

QString AppModel::spaceLine() const {
    qint64 free = freeSpace(paths_.data);
    if (free < 0) return "Needs about 13 GB.";
    return "Needs about 13 GB, " + QLocale().formattedDataSize(free, 1, QLocale::DataSizeTraditionalFormat) + " free.";
}

void AppModel::setHideWhilePlaying(bool on) {
    hide_ = on;
    QSettings().setValue("hideWhilePlaying", on);
    emit changed();
}

/// The game's own logo, when the launcher was packaged with one (scripts/package-windows.sh copies
/// local/branding/splatoon3-logo.png beside it): nothing of the game's goes into git, so a build without it shows the
/// title in type instead
QUrl AppModel::logo() const {
    QString path = QDir(QCoreApplication::applicationDirPath()).filePath("splatoon3-logo.png");
    if (!QFileInfo::exists(path)) path = qEnvironmentVariable("INKYPAINT_LOGO");
    return path.isEmpty() || !QFileInfo::exists(path) ? QUrl() : QUrl::fromLocalFile(path);
}

void AppModel::useDataFolder(const QString& folder) {
    QUrl u(folder);
    QString path = u.isLocalFile() ? u.toLocalFile() : folder;
    paths_.data = QDir::cleanPath(path);
    settings_.setPath(paths_.settings());
    if (paths_.data == QDir::cleanPath(GamePaths::defaultData())) QSettings().remove("dataFolder");
    else QSettings().setValue("dataFolder", paths_.data);
    emit changed();
}

void AppModel::refresh() {
    settings_.reload();
    mods_.refresh();
    emit changed();
}

void AppModel::prepare() { preparer_.start(&sources_, paths_); }
void AppModel::recompile() {
    setPane("files");
    preparer_.reset();
    prepare();
}

void AppModel::setSetupStep(const QString& step) {
    if (setupStep_ == step) return;
    setupStep_ = step;
    emit changed();
}
void AppModel::startPrepare() {
    if (preparer_.phase() == "done" || preparer_.phase() == "failed") preparer_.reset();
    prepare();
}
void AppModel::requestPrepare() {
    if (setupStep_ != "idle" || preparer_.running()) return;
    if (online_.signedIn()) prepareWithPatches();
    else setSetupStep("askingOnline");
}
void AppModel::prepareOffline() {
    if (setupStep_ != "askingOnline") return;
    setSetupStep("idle");
    setupOnline_ = false;
    startPrepare();
}
void AppModel::cancelSetup() {
    if (setupStep_ == "askingOnline") setSetupStep("idle");
}
/// The patches go where the recompile reads mods from (it never clears them), so they are there before the game is.
/// A failed download doesn't stop the setup: the Online page offers them again, at the cost of a recompile
void AppModel::prepareWithPatches() {
    if (setupStep_ == "downloadingPatches" || preparer_.running() || !online_.signedIn()) return;
    setupOnline_ = true;
    setSetupStep("downloadingPatches");
    Grid0Patches* patches = online_.patches();
    auto finish = [this, patches] {
        if (patches->status() == "checking") return;
        disconnect(patchesWait_);
        setSetupStep("idle");
        startPrepare();
    };
    disconnect(patchesWait_);
    patchesWait_ = connect(patches, &Grid0Patches::changed, this, finish);
    patches->update(paths_);
    finish(); // already finished (or already checking, which it waits for)
}

/// The game runs a copy of the runtime named after it (ScarletNX finds the game's code by that name). A newer launcher
/// brings a newer runtime: it is copied over the old one before the game starts
bool AppModel::deployRuntime(QString* error) {
    QString runtime = Bundled::runtime(), target = paths_.executable();
    QFileInfo a(runtime), b(target);
    if (!a.exists()) return true; // a development run without one: the game's own copy
#ifdef Q_OS_WIN
    // its DLLs too (a newer launcher's LLVM), which Windows finds beside the game's copy
    for (const QFileInfo& dll : QDir(a.absolutePath()).entryInfoList({"*.dll"}, QDir::Files)) {
        QString to = QDir(paths_.gameDir()).filePath(dll.fileName());
        QFileInfo have(to);
        if (have.exists() && have.size() == dll.size() && dll.lastModified() <= have.lastModified()) continue;
        QFile::remove(to);
        if (!QFile::copy(dll.filePath(), to)) {
            *error = "Couldn't copy " + dll.fileName() + " into the game's folder. Is the game still running?";
            return false;
        }
    }
#endif
    if (a.size() == b.size() && a.lastModified() <= b.lastModified()) return true;
    QString tmp = target + ".new";
    QFile::remove(tmp);
    if (!QFile::copy(runtime, tmp)) {
        *error = "Couldn't copy the runtime into the game's folder.";
        return false;
    }
    QFile(tmp).setPermissions(QFile(tmp).permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser);
    QFile::remove(target);
    if (!QFile::rename(tmp, target)) {
        *error = "Couldn't replace the game's copy of the runtime. Is the game still running?";
        return false;
    }
    return true;
}

void AppModel::play() {
    if (game_ || !prepared()) return;
    lastExit_.clear();
    QString why;
    if (!deployRuntime(&why)) {
        lastExit_ = "The game couldn't start: " + why;
        emit changed();
        return;
    }
    QDir().mkpath(GamePaths::logs());
    QString log = QDir(GamePaths::logs()).filePath("game-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".log");
    // online, the game signs in to GRID0+ itself: the password goes on its stdin, never on its command line or in the
    // game's settings
    QString password = online_.passwordForGame();
    game_ = new QProcess(this);
    game_->setProgram(paths_.executable());
    game_->setWorkingDirectory(paths_.gameDir());
    game_->setProcessEnvironment(Bundled::environment(!password.isNull()));
    game_->setProcessChannelMode(QProcess::MergedChannels);
    game_->setStandardOutputFile(log);
    connect(game_, &QProcess::finished, this, &AppModel::ended);
    connect(game_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart) return;
        lastExit_ = "The game couldn't start: " + game_->errorString();
        game_->deleteLater();
        game_ = nullptr;
        emit changed();
    });
    game_->start();
    if (!game_ || !game_->waitForStarted(10000)) return;
    if (!password.isNull()) game_->write((password + "\n").toUtf8());
    game_->closeWriteChannel();
    started_ = QDateTime::currentDateTime();
    QSettings().setValue("lastPlayed", started_);
    if (hide_)
        QTimer::singleShot(1500, this, [this] {
            if (game_) emit minimizeRequested();
        });
    emit changed();
}

void AppModel::stop() {
    stopping_ = game_ != nullptr;
    if (game_) game_->terminate(); // Windows: WM_CLOSE to its window, which the runtime takes as quitting
    QProcess* p = game_;
    QTimer::singleShot(5000, this, [this, p] {
        if (game_ == p && game_) game_->kill();
    });
}

void AppModel::ended(int status, QProcess::ExitStatus how) {
    qint64 ran = started_.isValid() ? started_.secsTo(QDateTime::currentDateTime()) : 0;
    game_->deleteLater();
    game_ = nullptr;
    settings_.reload();
    emit restoreRequested();
    // a game that stops by itself within seconds didn't start: say so, with where to look
    bool asked = stopping_;
    stopping_ = false;
    if (!asked && (how == QProcess::CrashExit || (status != 0 && ran < 20)))
        lastExit_ = QString("The game stopped unexpectedly (status %1). Its log is in %2.").arg(status).arg(displayPath(paths_.gameLog()));
    emit changed();
}
