// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
#include "modfiles.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QDebug>
#define CHECK(x) do { if (!(x)) { qCritical() << __LINE__ << #x; return 1; } } while (0)
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temp; CHECK(temp.isValid());
    const QString mod = temp.filePath("installed"), nested = mod + "/download/atmosphere/contents/0100C2500FC20000";
    CHECK(QDir().mkpath(nested + "/RomFS/Model"));
    QFile readme(mod + "/readme.txt"); CHECK(readme.open(QIODevice::WriteOnly)); readme.write("preserve"); readme.close();
    CHECK(!ModFiles::hasRomfs(mod));
    CHECK(ModFiles::normalizeInstalled(mod).isEmpty());
    CHECK(ModFiles::hasRomfs(mod));
    CHECK(QFileInfo::exists(mod + "/romfs/Model") && QFileInfo::exists(readme.fileName()));
    CHECK(!QFileInfo::exists(nested + "/RomFS"));
    CHECK(ModFiles::normalizeInstalled(mod).isEmpty());
    CHECK(ModFiles::roots(mod + "/romfs") == QStringList{mod});
    QString error;
    CHECK(ModFiles::install(mod, temp.filePath("copied"), &error));
    CHECK(ModFiles::hasRomfs(temp.filePath("copied")));
    CHECK(QDir().mkpath(temp.filePath("empty")));
    CHECK(!ModFiles::install(temp.filePath("empty"), temp.filePath("empty-install"), &error));
    CHECK(!QFileInfo::exists(temp.filePath("empty-install")));
    const QString multiple = temp.filePath("multiple");
    CHECK(QDir().mkpath(multiple + "/one/romfs") && QDir().mkpath(multiple + "/two/romfs"));
    CHECK(!ModFiles::normalizeInstalled(multiple).isEmpty());
    CHECK(QFileInfo::exists(multiple + "/one/romfs") && !ModFiles::hasRomfs(multiple));
    const QString bundle = temp.filePath("sd-bundle"), title = bundle + "/atmosphere/contents/0100c2500fc20000";
    CHECK(QDir().mkpath(title + "/exefs") && QDir().mkpath(bundle + "/flexlion3"));
    QFile module(title + "/exefs/subsdk0"); CHECK(module.open(QIODevice::WriteOnly)); module.write("fixture"); module.close();
    QFile asset(bundle + "/flexlion3/config.ini"); CHECK(asset.open(QIODevice::WriteOnly)); asset.write("fixture"); asset.close();
    const QString installed = temp.filePath("sd-installed");
    CHECK(ModFiles::install(title, installed, &error, bundle));
    CHECK(ModFiles::hasCode(installed) && ModFiles::hasSdmc(installed));
    CHECK(QFileInfo::exists(installed + "/sdmc/flexlion3/config.ini"));
    CHECK(QFileInfo::exists(asset.fileName())); // import copies, never moves the source package
    const QString limited = temp.filePath("limited");
    CHECK(ModFiles::install(title, limited, &error, title));
    CHECK(!ModFiles::hasSdmc(limited)); // do not import folders above the selected source
    CHECK(ModFiles::normalizeInstalled(bundle).isEmpty());
    CHECK(ModFiles::hasCode(bundle) && ModFiles::hasSdmc(bundle));
    CHECK(QFileInfo::exists(bundle + "/sdmc/flexlion3/config.ini"));
    CHECK(QDir().mkpath(mod + "/exefs"));
    QFile code(mod + "/exefs/main"); CHECK(code.open(QIODevice::WriteOnly)); code.write("fixture"); code.close();
    CHECK(ModFiles::install(mod, temp.filePath("romfs-only"), &error, mod + "/romfs"));
    CHECK(!ModFiles::hasCode(temp.filePath("romfs-only")));
    const QString overlap = temp.filePath("overlap");
    CHECK(QDir().mkpath(overlap + "/romfs"));
    CHECK(!ModFiles::install(overlap, overlap + "/romfs/inside", &error));
    qInfo() << "PASS mod paths: nested/cased payload repair, preserved wrapper, direct romfs selection, install validation, ambiguous roots";
}
