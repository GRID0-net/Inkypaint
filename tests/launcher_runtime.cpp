// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
#include "game.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <cstdio>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "failed line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static bool file(const QString& path, bool executable = true) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write("synthetic runtime\n"); f.close();
    return f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                            (executable ? QFileDevice::ExeOwner : QFileDevice::Permissions{}));
}
int main(int argc, char** argv) {
    QCoreApplication app(argc,argv);
    qunsetenv("INKYPAINT_RUNTIME");
    QTemporaryDir temp;
    CHECK(temp.isValid());
#ifdef Q_OS_WIN
    const QString suffix = ".exe", primary = "windows";
#elif defined(Q_OS_MACOS)
    const QString suffix, primary = "macos";
#else
    const QString suffix, primary = "linux";
#endif
    const QString root = temp.path(), directory = root + "/build/launcher/bin";
    CHECK(QDir().mkpath(directory));
    const QString packaged = directory + "/runtime/scarletnx" + suffix;
    CHECK(Bundled::runtimeIn(directory) == packaged);
    CHECK(file(root + "/src/nx/CMakeLists.txt"));
    CHECK(file(root + "/src/launcher-qt/CMakeLists.txt"));
    const QString native = root + "/build/" + primary + "/scarletnx" + suffix;
    CHECK(file(native));
    CHECK(Bundled::runtimeIn(directory) == native);
    CHECK(file(packaged));
    CHECK(Bundled::runtimeIn(directory) == packaged);
    const QString override = root + "/custom/scarletnx" + suffix;
    CHECK(file(override));
    qputenv("INKYPAINT_RUNTIME", override.toUtf8());
    CHECK(Bundled::runtimeIn(directory) == override);
    CHECK(Bundled::isComplete());
    CHECK(file(override, false));
    CHECK(!Bundled::isComplete());
    qputenv("INKYPAINT_RUNTIME", root.toUtf8());
    CHECK(!Bundled::isComplete()); // a traversable directory is not an executable
    qputenv("INKYPAINT_RUNTIME", (root + "/missing").toUtf8());
    CHECK(Bundled::runtimeIn(directory) == root + "/missing");
    CHECK(!Bundled::isComplete());
    qunsetenv("INKYPAINT_RUNTIME");
    puts("PASS launcher runtime: packaged/source/override precedence, permissions and missing executable");
}
