// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
#pragma once
#include "discordipc.h"
#include <QElapsedTimer>
#include <QLocalSocket>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <optional>

/// The connection to the Discord client on this machine: it finds discord-ipc-0 to 9, says hello with the
/// application's ID and shows the activity it was last given, at most once every 2 seconds (Discord's rate limit).
/// Without Discord it tries again every 15 seconds. An empty application ID, or turned off, it does nothing at all
class DiscordPresence : public QObject {
    Q_OBJECT
public:
    explicit DiscordPresence(const QString& clientId, QObject* parent = nullptr);
    ~DiscordPresence() override;
    void setEnabled(bool on);
    /// What to show, or nothing to clear it
    void setActivity(const std::optional<DiscordIpc::Activity>& activity);
    /// Clears the activity and closes, waiting a moment for the bytes to go: for the launcher's exit only
    void shutdown();

private:
    static QStringList candidates();
    void connectNext();
    void lost();
    void received();
    void send(quint32 opcode, const QByteArray& payload);
    void flush();

    QString clientId_;
    bool enabled_ = true;
    bool open_ = false;  // connected, the handshake sent
    bool ready_ = false; // Discord answered READY
    QLocalSocket socket_;
    QByteArray buffer_;
    QStringList candidates_;
    int candidate_ = 0;
    QTimer retry_, debounce_;
    QElapsedTimer sentAt_;
    std::optional<DiscordIpc::Activity> wanted_, shown_;
    bool shownValid_ = false; // shown_ is what Discord has: false after a reconnect, so it is sent again
    quint64 nonce_ = 0;
};
