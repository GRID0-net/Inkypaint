// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
// The inkypaint launcher for Windows and Linux: Qt 6 Quick, the same panes as the Mac's SwiftUI one
// (src/launcher). It sets the game up from the player's own files with the bundled runtime, edits its settings, signs
// in to GRID0+ and starts the game
#include "appmodel.h"
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

int main(int argc, char** argv) {
    QGuiApplication::setOrganizationName(QString());
    QGuiApplication::setApplicationName("inkypaint");
    QGuiApplication::setApplicationDisplayName("inkypaint");
    QGuiApplication::setDesktopFileName("inkypaint"); // Wayland matches the window to inkypaint.desktop by it
    QGuiApplication app(argc, argv);
    app.setWindowIcon(QIcon(":/inkypaint/assets/AppIcon.png"));
    QQuickStyle::setStyle("Basic"); // drawn by the app's own components, the same on every system
    AppModel model;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("app", &model);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
                     Qt::QueuedConnection);
    engine.loadFromModule("Inkypaint", "Main");
    if (engine.rootObjects().isEmpty()) return 1;
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    QObject::connect(&model, &AppModel::minimizeRequested, window, [window] { window->showMinimized(); });
    QObject::connect(&model, &AppModel::restoreRequested, window, [window] {
        window->showNormal();
        window->raise();
        window->requestActivate();
    });
    // for working on the UI: INKYPAINT_PANE opens a pane, INKYPAINT_SCREENSHOT=file.png saves the window once it has
    // settled and quits
    if (!qEnvironmentVariable("INKYPAINT_PANE").isEmpty()) model.setPane(qEnvironmentVariable("INKYPAINT_PANE"));
    QString shot = qEnvironmentVariable("INKYPAINT_SCREENSHOT");
    if (!shot.isEmpty())
        QTimer::singleShot(2500, window, [window, shot] {
            window->grabWindow().save(shot);
            QCoreApplication::quit();
        });
    return app.exec();
}
