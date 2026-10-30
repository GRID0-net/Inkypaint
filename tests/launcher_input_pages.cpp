// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
#include "gamesettings.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlComponent>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QDebug>
#include <QQuickItem>
#define CHECK(x) do { if (!(x)) { qCritical() << __LINE__ << #x; return 1; } } while (0)
static QQuickItem* visual(QQuickItem* item, const QString& name) {
    if (item->objectName() == name) return item;
    for (auto child : item->childItems()) if (auto found = visual(child, name)) return found;
    return nullptr;
}
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QTemporaryDir dir; CHECK(dir.isValid());
    QDir source(QStringLiteral(INKY_QML_DIR));
    for (const auto& name : source.entryList({"*.qml"}, QDir::Files)) CHECK(QFile::copy(source.filePath(name), dir.filePath(name)));
    QFile module(dir.filePath("qmldir")); CHECK(module.open(QIODevice::WriteOnly));
    module.write("singleton Theme 1.0 Theme.qml\n"); module.close();
    GameSettings settings; settings.setPath(dir.filePath("game.ini"));
    settings.set("keyboard_mouse_enabled", "1");
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("contextSettings", &settings);
    QQmlComponent fixture(&engine);
    fixture.setData("import QtQml\nQtObject { property var settings: contextSettings; property bool prepared: true; property string pane: 'settings' }", QUrl());
    QScopedPointer<QObject> context(fixture.create()); CHECK(context);
    engine.rootContext()->setContextProperty("app", context.data());
    QStringList problems;
    QObject::connect(&engine, &QQmlEngine::warnings, [&](const QList<QQmlError>& errors) {
        for (const auto& error : errors) {
            if (!error.description().contains("Cannot open: qrc:/inkypaint/assets/")) problems.append(error.toString());
        }
    });
    for (const auto& page : {QString(), QStringLiteral("keyboard"), QStringLiteral("controller")}) {
        QQmlComponent component(&engine, QUrl::fromLocalFile(dir.filePath("SettingsPage.qml")));
        QScopedPointer<QObject> object(component.createWithInitialProperties({{"inputPage", page}, {"width", 1000}, {"height", 850}}));
        CHECK(object);
        QCoreApplication::processEvents();
        CHECK(problems.isEmpty());
        CHECK(visual(qobject_cast<QQuickItem*>(object.data()), page == "keyboard" ? "Keyboard and mouse" : page == "controller" ? "Controller input" : "Controls"));
        if (page.isEmpty()) {
            CHECK(!visual(qobject_cast<QQuickItem*>(object.data()), "Keyboard and mouse"));
            CHECK(!visual(qobject_cast<QQuickItem*>(object.data()), "Controller input"));
            auto fps = visual(qobject_cast<QQuickItem*>(object.data()), "fps_limitChoice");
            CHECK(fps && !fps->isEnabled());
            settings.set("smooth_motion", "1");
            QCoreApplication::processEvents();
            CHECK(fps->isEnabled());
            settings.set("fps_limit", "100");
            settings.set("smooth_motion", "0");
            QCoreApplication::processEvents();
            CHECK(!fps->isEnabled() && settings.value("fps_limit") == "game");
        }
    }
    qInfo() << "PASS Qt input pages: main, keyboard, controller, QML bindings";
}
