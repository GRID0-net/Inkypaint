// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
#include "launchersettings.h"
#include <QSettings>
#include <QCoreApplication>

void initializeLauncherIdentity() {
    QCoreApplication::setOrganizationName("GRID0");
    QCoreApplication::setApplicationName("inkypaint");
}

void migrateLauncherSettings() {
    QSettings current;
    if (current.value("legacySettingsImported", false).toBool()) return;
    QSettings legacy(QSettings::NativeFormat, QSettings::UserScope, QString(), "inkypaint");
    for (const QString& key : legacy.allKeys())
        if (!current.contains(key)) current.setValue(key, legacy.value(key));
    current.setValue("legacySettingsImported", true);
    current.sync();
}
