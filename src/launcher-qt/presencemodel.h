// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
#pragma once
#include "discordpresence.h"
#include <QObject>
#include <QTimer>

class AppModel;

/// The launcher's Discord rich presence: follows the page, the preparation and the game (with the runtime's status
/// file while it runs, read every 2 seconds) and hands DiscordPresence what to show. The application ID is
/// INKYPAINT_DISCORD_APP_ID from the environment, else the build's: without one nothing connects
class PresenceModel : public QObject {
    Q_OBJECT
public:
    explicit PresenceModel(AppModel* app, QObject* parent = nullptr);
    static QString applicationId();
    void shutdown(); // clears what Discord shows, at the launcher's exit

private:
    void update();
    AppModel* app_;
    DiscordPresence discord_;
    QTimer poll_;
    qint64 launcherStart_;
};
