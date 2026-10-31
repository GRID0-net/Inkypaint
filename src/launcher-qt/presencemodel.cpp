// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
#include "presencemodel.h"
#include "appmodel.h"
#include "presencescene.h"
#include <QDateTime>
#include <QFile>

#ifndef INKYPAINT_DISCORD_APP_ID
#define INKYPAINT_DISCORD_APP_ID ""
#endif

QString PresenceModel::applicationId() {
    QString id = qEnvironmentVariable("INKYPAINT_DISCORD_APP_ID", QStringLiteral(INKYPAINT_DISCORD_APP_ID)).trimmed();
    for (QChar c : id)
        if (!c.isDigit()) return {}; // Discord's IDs are numbers: anything else would only be refused
    return id;
}

PresenceModel::PresenceModel(AppModel* app, QObject* parent)
    : QObject(parent), app_(app), discord_(applicationId()),
      launcherStart_(QDateTime::currentSecsSinceEpoch()) {
    discord_.setEnabled(app_->discordPresence());
    connect(app_, &AppModel::changed, this, &PresenceModel::update);
    connect(app_, &AppModel::paneChanged, this, &PresenceModel::update);
    poll_.setInterval(2000);
    connect(&poll_, &QTimer::timeout, this, &PresenceModel::update);
    update();
}

void PresenceModel::update() {
    discord_.setEnabled(app_->discordPresence());
    if (app_->playing() != poll_.isActive()) {
        if (app_->playing()) poll_.start();
        else poll_.stop();
    }
    if (!app_->discordPresence() || applicationId().isEmpty()) return;
    Presence::LauncherState s;
    s.pane = app_->pane();
    s.phase = app_->preparer()->phase();
    s.fraction = app_->preparer()->fraction();
    s.playing = app_->playing();
    s.launcherStart = launcherStart_;
    s.now = QDateTime::currentSecsSinceEpoch();
    s.gameStart = app_->gameStarted().isValid() ? app_->gameStarted().toSecsSinceEpoch() : s.now;
    if (s.playing) {
        QFile file(app_->presenceFile());
        if (file.size() < 4096 && file.open(QIODevice::ReadOnly))
            s.game = Presence::parseStatus(file.readAll(), app_->gamePid(), QDateTime::currentMSecsSinceEpoch());
    }
    discord_.setActivity(Presence::describe(s)); // DiscordPresence skips one already shown and keeps to its rate
}

void PresenceModel::shutdown() {
    poll_.stop();
    discord_.shutdown();
}
