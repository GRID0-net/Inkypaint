// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QFile>
#include <QTemporaryDir>
#include <QDebug>
#define CHECK(x) do { if (!(x)) { qCritical() << __LINE__ << #x; return 1; } } while (0)
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QTemporaryDir dir;
    CHECK(dir.isValid());
    CHECK(QFile::copy(QStringLiteral(INKY_QML_DIR "/InkSwitch.qml"), dir.filePath("InkSwitch.qml")));
    CHECK(QFile::copy(QStringLiteral(INKY_QML_DIR "/Theme.qml"), dir.filePath("Theme.qml")));
    QFile module(dir.filePath("qmldir"));
    CHECK(module.open(QIODevice::WriteOnly));
    module.write("singleton Theme 1.0 Theme.qml\n"); module.close();
    QFile test(dir.filePath("Test.qml"));
    CHECK(test.open(QIODevice::WriteOnly));
    test.write("import QtQuick\nWindow { width: 100; height: 70; visible: true; InkSwitch { objectName: 'toggle'; x: 20; y: 20; focus: true } }"); test.close();
    QQmlApplicationEngine engine;
    engine.load(QUrl::fromLocalFile(test.fileName()));
    CHECK(!engine.rootObjects().isEmpty());
    auto window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    CHECK(window);
    auto toggle = window->findChild<QObject*>("toggle"); CHECK(toggle);
    QCoreApplication::processEvents();
    // Every part of the visible track must click, including both edges and the thumb.
    for (int x : {1, 8, 23, 37, 45}) for (int y : {1, 13, 25}) {
        bool was = toggle->property("checked").toBool();
        QPointF point(20 + x, 20 + y);
        QMouseEvent down(QEvent::MouseButtonPress, point, window->mapToGlobal(point.toPoint()), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent up(QEvent::MouseButtonRelease, point, window->mapToGlobal(point.toPoint()), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &down); QCoreApplication::sendEvent(window, &up);
        CHECK(toggle->property("checked").toBool() != was);
    }
    bool was = toggle->property("checked").toBool();
    QKeyEvent down(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier), up(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &down); QCoreApplication::sendEvent(window, &up);
    CHECK(toggle->property("checked").toBool() != was);
    toggle->setProperty("enabled", false);
    was = toggle->property("checked").toBool();
    QCoreApplication::sendEvent(window, &down); QCoreApplication::sendEvent(window, &up);
    CHECK(toggle->property("checked").toBool() == was);
    qInfo() << "PASS switch: 15 track positions, keyboard and disabled state";
}
