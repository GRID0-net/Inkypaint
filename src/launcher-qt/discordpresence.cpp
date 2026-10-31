// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
#include "discordpresence.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

DiscordPresence::DiscordPresence(const QString& clientId, QObject* parent) : QObject(parent), clientId_(clientId) {
    retry_.setSingleShot(true);
    retry_.setInterval(15000);
    debounce_.setSingleShot(true);
    connect(&retry_, &QTimer::timeout, this, &DiscordPresence::connectNext);
    connect(&debounce_, &QTimer::timeout, this, &DiscordPresence::flush);
    connect(&socket_, &QLocalSocket::connected, this, [this] {
        open_ = true;
        candidate_ = 0;
        buffer_.clear();
        send(DiscordIpc::Handshake, DiscordIpc::handshake(clientId_));
    });
    connect(&socket_, &QLocalSocket::readyRead, this, &DiscordPresence::received);
    connect(&socket_, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError) {
        if (open_) lost();
        else QTimer::singleShot(0, this, &DiscordPresence::connectNext); // that one is not there: the next
    });
    connect(&socket_, &QLocalSocket::disconnected, this, [this] {
        if (open_) lost();
    });
    if (!clientId_.isEmpty()) QTimer::singleShot(0, this, &DiscordPresence::connectNext);
}

DiscordPresence::~DiscordPresence() { shutdown(); }

QStringList DiscordPresence::candidates() {
    QStringList names;
#if defined(Q_OS_WIN)
    // Qt makes \\.\pipe\discord-ipc-N of a plain name
    for (int n = 0; n < 10; n++) names << QStringLiteral("discord-ipc-%1").arg(n);
#else
    QStringList bases;
    for (const char* var : {"XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP"}) {
        QString dir = qEnvironmentVariable(var);
        if (!dir.isEmpty()) bases << dir;
    }
    bases << QStringLiteral("/tmp");
    // the Flatpak and Snap packages of Discord, and Vesktop's Flatpak, put theirs in their own folders
    const QStringList within = {QString(), QStringLiteral("app/com.discordapp.Discord"), QStringLiteral("snap.discord"),
                                QStringLiteral(".flatpak/dev.vencord.Vesktop/xdg-run")};
    for (const QString& base : bases)
        for (const QString& sub : within) {
            QString dir = sub.isEmpty() ? base : QDir(base).filePath(sub);
            for (int n = 0; n < 10; n++) {
                QString path = QDir(dir).filePath(QStringLiteral("discord-ipc-%1").arg(n));
                if (QFileInfo::exists(path) && !names.contains(path)) names << path;
            }
        }
#endif
    return names;
}

void DiscordPresence::connectNext() {
    if (!enabled_ || clientId_.isEmpty() || open_ || socket_.state() == QLocalSocket::ConnectingState) return;
    if (candidate_ == 0) candidates_ = candidates();
    if (candidate_ >= candidates_.size()) { // none answered: Discord is not running yet
        candidate_ = 0;
        retry_.start();
        return;
    }
    socket_.abort();
    socket_.connectToServer(candidates_[candidate_++]);
}

void DiscordPresence::lost() {
    open_ = false;
    ready_ = false;
    shownValid_ = false;
    buffer_.clear();
    debounce_.stop();
    socket_.abort();
    candidate_ = 0;
    if (enabled_ && !clientId_.isEmpty()) retry_.start();
}

void DiscordPresence::received() {
    buffer_ += socket_.readAll();
    quint32 opcode = 0;
    QByteArray payload;
    for (;;) {
        int got = DiscordIpc::take(buffer_, opcode, payload);
        if (got == 0) return;
        if (got < 0) {
            lost();
            return;
        }
        if (opcode == DiscordIpc::Ping) {
            send(DiscordIpc::Pong, payload);
        } else if (opcode == DiscordIpc::Close) {
            lost(); // a wrong application ID ends here too, and is tried again later like a missing Discord
            return;
        } else if (opcode == DiscordIpc::Frame) {
            QJsonObject o = QJsonDocument::fromJson(payload).object();
            if (o.value("cmd").toString() == "DISPATCH" && o.value("evt").toString() == "READY") {
                ready_ = true;
                flush();
            }
        }
    }
}

void DiscordPresence::send(quint32 opcode, const QByteArray& payload) {
    if (socket_.state() == QLocalSocket::ConnectedState) socket_.write(DiscordIpc::encode(opcode, payload));
}

void DiscordPresence::flush() {
    if (!ready_ || (shownValid_ && wanted_ == shown_)) return;
    if (sentAt_.isValid() && sentAt_.elapsed() < 2000) {
        if (!debounce_.isActive()) debounce_.start(int(2000 - sentAt_.elapsed()));
        return;
    }
    QString nonce = QString::number(QCoreApplication::applicationPid()) + "-" + QString::number(++nonce_);
    send(DiscordIpc::Frame, DiscordIpc::setActivity(QCoreApplication::applicationPid(),
                                                    wanted_ ? &*wanted_ : nullptr, nonce));
    shown_ = wanted_;
    shownValid_ = true;
    sentAt_.start();
}

void DiscordPresence::setActivity(const std::optional<DiscordIpc::Activity>& activity) {
    wanted_ = activity;
    flush();
}

void DiscordPresence::setEnabled(bool on) {
    if (enabled_ == on) return;
    if (!on) {
        if (ready_) send(DiscordIpc::Frame, DiscordIpc::setActivity(QCoreApplication::applicationPid(), nullptr, "off"));
        socket_.flush();
        enabled_ = false;
        retry_.stop();
        lost();
        return;
    }
    enabled_ = true;
    connectNext();
}

void DiscordPresence::shutdown() {
    retry_.stop();
    debounce_.stop();
    if (ready_) {
        send(DiscordIpc::Frame, DiscordIpc::setActivity(QCoreApplication::applicationPid(), nullptr, "exit"));
        socket_.waitForBytesWritten(200); // the launcher is closing: a moment is all it waits
    }
    enabled_ = false;
    open_ = false;
    ready_ = false;
    socket_.abort();
}
