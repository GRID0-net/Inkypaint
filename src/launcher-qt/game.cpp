// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
#include "game.h"
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QUrl>

#ifdef Q_OS_WIN
static const QString exe = QStringLiteral(".exe");
#else
static const QString exe;
#endif

QString GamePaths::appData() {
    // AppLocalDataLocation is %LOCALAPPDATA%\<org>\<app>: the organisation is left empty, so it is ...\inkypaint
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return QDir(base).filePath(QStringLiteral("inkypaint"));
}
QString GamePaths::defaultData() { return QDir(appData()).filePath(QStringLiteral("data")); }
QString GamePaths::logs() { return QDir(appData()).filePath(QStringLiteral("logs")); }
QString GamePaths::work() const { return QDir(data).filePath(QStringLiteral("work/splatoon3")); }
QString GamePaths::recompiled() const { return QDir(data).filePath(QStringLiteral("recompiled")); }
QString GamePaths::gameDir() const { return QDir(recompiled()).filePath(Game::titleID); }
QString GamePaths::executable() const { return QDir(gameDir()).filePath(Game::titleID + exe); }
QString GamePaths::settings() const { return QDir(gameDir()).filePath(Game::titleID + QStringLiteral(".ini")); }
QString GamePaths::gameLog() const { return QDir(gameDir()).filePath(Game::titleID + QStringLiteral(".log")); }
bool GamePaths::isExtracted() const {
    return QFileInfo::exists(QDir(work()).filePath(QStringLiteral("exefs/main"))) &&
           QFileInfo(QDir(work()).filePath(QStringLiteral("romfs"))).isDir();
}
bool GamePaths::isPrepared() const {
    QFileInfo f(executable());
    return f.isFile() && f.isExecutable();
}

QString Bundled::runtime() {
    QString env = qEnvironmentVariable("INKYPAINT_RUNTIME");
    if (!env.isEmpty()) return env;
    // in a folder of its own with only its DLLs: the recompile copies every DLL beside it into the game's folder,
    // where Windows looks for them (snx_deploy_runtime), and the launcher's Qt has no business there
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("runtime/scarletnx") + exe);
}
bool Bundled::isComplete() { return QFileInfo(runtime()).isExecutable(); }
QProcessEnvironment Bundled::environment(bool grid0Stdin) {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (grid0Stdin) env.insert(QStringLiteral("INKYPAINT_GRID0_STDIN"), QStringLiteral("1"));
    return env;
}

qint64 freeSpace(const QString& path) {
    QString p = path;
    while (!p.isEmpty() && !QFileInfo::exists(p)) {
        QString up = QFileInfo(p).absolutePath();
        if (up == p) break;
        p = up;
    }
    QStorageInfo s(p);
    return s.isValid() ? s.bytesAvailable() : -1;
}

QString displayPath(const QString& path) {
    QString p = QDir::toNativeSeparators(QDir::cleanPath(path));
#ifndef Q_OS_WIN
    QString home = QDir::homePath();
    if (p == home || p.startsWith(home + QLatin1Char('/'))) p = QLatin1Char('~') + p.mid(home.size());
#endif
    return p;
}

void revealInFileManager(const QString& path) {
#ifdef Q_OS_WIN
    if (QFileInfo::exists(path)) {
        QProcess::startDetached(QStringLiteral("explorer.exe"),
                                {QStringLiteral("/select,") + QDir::toNativeSeparators(path)});
        return;
    }
#endif
    QFileInfo f(path);
    QDesktopServices::openUrl(QUrl::fromLocalFile(f.isDir() ? path : f.absolutePath()));
}
