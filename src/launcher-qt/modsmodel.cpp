// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
#include "modsmodel.h"
#include "modfiles.h"
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>

static const QString grid0Prefix = "grid0plus-"; // the Online page's
static const QString section = "Game";

ModsModel::ModsModel(const GamePaths* paths, GameSettings* settings, QObject* parent)
    : QObject(parent), paths_(paths), settings_(settings) {}

QString ModsModel::folder() const { return QDir(paths_->gameDir()).filePath(Game::titleID + "_data/mods"); }

void ModsModel::refresh() {
    const QString off = settings_->value("mods_off"), dir = folder();
    mods_.clear();
    for (const QString& name : ModFiles::list(dir)) {
        if (name.startsWith(grid0Prefix)) continue;
        QString mod = QDir(dir).filePath(name);
        const QString issue = ModFiles::normalizeInstalled(mod);
        mods_.append(QVariantMap{{"name", name},
                                 {"issue", issue},
                                 {"on", !ModFiles::isOff(off, name)},
                                 {"romfs", ModFiles::hasRomfs(mod)},
                                 {"sdmc", ModFiles::hasSdmc(mod)},
                                 {"code", ModFiles::hasCode(mod)}});
    }
    // the code the game was recompiled with, against the code mods played now (GRID0+'s included)
    needsRecompile_ = paths_->isPrepared() &&
                      ModFiles::stamp(dir, off) != ModFiles::recorded(QDir(paths_->gameDir()).filePath(Game::titleID + ".abi"));
    emit changed();
}

void ModsModel::setOn(const QString& name, bool on) {
    settings_->setRaw("mods_off", ModFiles::setOff(settings_->value("mods_off"), name, !on), section);
    refresh();
}

void ModsModel::remove(const QString& name) {
    if (name.isEmpty() || name.contains('/') || name.contains('\\') || name.startsWith('.')) return;
    if (!QDir(QDir(folder()).filePath(name)).removeRecursively()) error_ = "Couldn't remove all of " + name + ". Is the game running?";
    else notice_ = "Removed " + name + ".";
    settings_->setRaw("mods_off", ModFiles::setOff(settings_->value("mods_off"), name, false), section);
    refresh();
}

void ModsModel::reveal() {
    QDir().mkpath(folder());
    revealInFileManager(folder());
}

void ModsModel::dismiss() {
    error_.clear();
    notice_.clear();
    emit changed();
}

/// An archive unpacked into dir: Windows' own tar (bsdtar, which reads .zip, .7z and .rar), or what the system has
static QString unpack(const QString& archive, const QString& dir) {
    struct Tool {
        QString name;
        QStringList args;
    };
    const QString lower = archive.toLower();
    QList<Tool> tools{{"bsdtar", {"-xf", archive, "-C", dir}}};
#ifdef Q_OS_WIN
    tools.prepend({QDir(qEnvironmentVariable("SystemRoot", "C:\\Windows")).filePath("System32/tar.exe"), {"-xf", archive, "-C", dir}});
#endif
    if (lower.endsWith(".zip")) tools << Tool{"unzip", {"-q", archive, "-d", dir}};
    tools << Tool{"7z", {"x", "-y", "-o" + dir, archive}} << Tool{"7za", {"x", "-y", "-o" + dir, archive}};
    bool anyTool = false;
    for (const Tool& t : tools) {
        QString program = QFileInfo(t.name).isAbsolute() ? t.name : QStandardPaths::findExecutable(t.name);
        if (program.isEmpty() || !QFileInfo::exists(program)) continue;
        anyTool = true;
        QProcess p;
        p.start(program, t.args);
        if (p.waitForFinished(10 * 60 * 1000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0) return {};
        QDir(dir).removeRecursively();
        QDir().mkpath(dir);
    }
    return anyTool ? "Couldn't unpack " + QFileInfo(archive).fileName() + ". Unpack it yourself and add the folder."
                   : "Nothing here can unpack " + QFileInfo(archive).fileName() + ". Unpack it yourself and add the folder.";
}

void ModsModel::add(const QString& source) {
    if (busy_) return;
    QUrl u(source);
    const QString path = QDir::cleanPath(u.isLocalFile() ? u.toLocalFile() : source), mods = folder();
    const QFileInfo info(path);
    if (!info.exists()) return;
    busy_ = true;
    error_.clear();
    notice_.clear();
    emit changed();
    // the copying (a RomFS mod can be large) and unpacking happen away from the window's thread
    QThread* worker = QThread::create([this, path, mods, info] {
        QStringList added;
        QString error;
        QDir().mkpath(mods);
        QTemporaryDir unpacked(QFileInfo(mods).path() + "/.mod-unpacking-XXXXXX"); // beside mods/, never in it
        QString base = path, fallback = info.isDir() ? info.fileName() : info.completeBaseName();
        if (!info.isDir()) {
            if (!unpacked.isValid()) error = "Couldn't make room to unpack the mod.";
            else if ((error = unpack(path, unpacked.path())).isEmpty()) base = unpacked.path();
        }
        if (error.isEmpty()) {
            const QStringList roots = ModFiles::roots(base);
            if (roots.isEmpty())
                error = fallback + " has no romfs or exefs folder, so it isn't a mod inkypaint can use. A mod keeps its "
                                   "files in romfs/ (and code patches in exefs/).";
            for (const QString& root : roots) {
                QString name = ModFiles::freeName(mods, root == base ? fallback : QFileInfo(root).fileName(), fallback);
                if (!ModFiles::install(root, QDir(mods).filePath(name), &error, base)) break;
                added << name;
            }
        }
        QMetaObject::invokeMethod(this, [this, added, error] { finished(added, error); }, Qt::QueuedConnection);
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void ModsModel::finished(const QStringList& added, const QString& error) {
    busy_ = false;
    error_ = error;
    notice_ = added.isEmpty() ? QString()
              : added.size() == 1 ? "Added " + added[0] + "."
                                  : "Added " + QString::number(added.size()) + " mods: " + added.join(", ") + ".";
    refresh();
}
