// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQuickStyle>

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    if (argc != 3) return 2;
    QQuickStyle::setStyle("Basic");
    QQmlApplicationEngine engine;
    // Use only the staged package's QML modules, not the developer's Qt tree.
    engine.setImportPathList({QString::fromLocal8Bit(argv[2]), "qrc:/qt-project.org/imports"});
    QObject::connect(&engine, &QQmlEngine::warnings, &app, [](const QList<QQmlError>& errors) {
        for (const QQmlError& error : errors) {
            QString message = error.description();
            if (message.contains("is not installed") || message.contains("failed to load") ||
                message.contains("is not available")) QCoreApplication::exit(1);
        }
    });
    engine.load(QUrl::fromLocalFile(QString::fromLocal8Bit(argv[1])));
    if (engine.rootObjects().isEmpty()) return 3;
    return app.exec();
}
