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

QString Bundled::runtimeIn(const QString& applicationDirectory) {
    const QString env = qEnvironmentVariable("INKYPAINT_RUNTIME");
    if (!env.isEmpty()) return QFileInfo(env).absoluteFilePath();
    const QDir application(applicationDirectory);
    const QString packaged = application.filePath(QStringLiteral("runtime/scarletnx") + exe);
    if (QFileInfo::exists(packaged)) return packaged;
    const QString beside = application.filePath(QStringLiteral("scarletnx") + exe);
    if (QFileInfo::exists(beside)) return beside;
    // Source builds have two CMake output directories. Recognize a checkout,
    // then inspect its known runtime outputs rather than searching arbitrary files.
    QDir parent(applicationDirectory);
    for (int depth = 0; depth < 7; ++depth) {
        if (QFileInfo::exists(parent.filePath(QStringLiteral("src/nx/CMakeLists.txt"))) &&
            QFileInfo::exists(parent.filePath(QStringLiteral("src/launcher-qt/CMakeLists.txt")))) {
#ifdef Q_OS_WIN
            const QStringList builds = {QStringLiteral("build/windows"), QStringLiteral("build/default"), QStringLiteral("build")};
#elif defined(Q_OS_MACOS)
            const QStringList builds = {QStringLiteral("build/macos"), QStringLiteral("build/default"), QStringLiteral("build")};
#else
            const QStringList builds = {QStringLiteral("build/linux"), QStringLiteral("build/default"), QStringLiteral("build")};
#endif
            for (const QString& build : builds) {
                const QString candidate = parent.filePath(build + QStringLiteral("/scarletnx") + exe);
                if (QFileInfo::exists(candidate)) return candidate;
            }
            break;
        }
        if (!parent.cdUp()) break;
    }
    return packaged;
}
QString Bundled::runtime() { return runtimeIn(QCoreApplication::applicationDirPath()); }
bool Bundled::isComplete() {
    const QFileInfo file(runtime());
    return file.isFile() && file.isExecutable();
}
QProcessEnvironment Bundled::environment(bool grid0Stdin) {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("INKYPAINT_LAUNCHER"), QStringLiteral("1")); // Escape asks the launcher first
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
