// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
#include "launchersettings.h"
#include "game.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>

int main(int argc, char** argv) {
    initializeLauncherIdentity();
    QCoreApplication app(argc, argv);
    if (app.organizationName().isEmpty()) return 1;
    if (argc == 3) {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, argv[2]);
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, QString(argv[2]) + "/system");
        QSettings settings;
        QString data = QString(argv[2]) + "/custom game data";
        if (QString(argv[1]) == "write") {
            settings.setValue("dataFolder", data);
            settings.setValue("grid0User", "fixture-user");
            settings.setValue("grid0Server", "fixture.invalid");
            settings.setValue("hideWhilePlaying", false);
            settings.sync();
            GamePaths paths{data};
            QDir().mkpath(paths.gameDir());
            QFile exe(paths.executable());
            if (!exe.open(QIODevice::WriteOnly)) return 2;
            exe.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        } else {
            if (settings.value("dataFolder").toString() != data ||
                settings.value("grid0User").toString() != "fixture-user" ||
                settings.value("grid0Server").toString() != "fixture.invalid" ||
                settings.value("hideWhilePlaying", true).toBool()) return 3;
            if (!GamePaths{settings.value("dataFolder").toString()}.isPrepared()) return 4;
        }
        return settings.status() == QSettings::NoError ? 0 : 5;
    }
    QTemporaryDir dir;
    if (!dir.isValid()) return 6;
    for (const QString& mode : {QString("write"), QString("read"), QString("read")}) {
        QProcess child;
        child.start(app.applicationFilePath(), {mode, dir.path()});
        if (!child.waitForFinished(10000) || child.exitStatus() != QProcess::NormalExit || child.exitCode()) return 7;
    }
    return 0;
}
