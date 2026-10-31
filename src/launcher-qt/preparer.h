// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
#pragma once
#include "game.h"
#include <QDateTime>
#include <QObject>
#include <QProcess>
#include <QStringList>

/// The files a preparation starts from: the base game's and its 11.3.0 update's NSPs, and the console's keys
class GameSources : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString base MEMBER base_ NOTIFY changed)
    Q_PROPERTY(QString update MEMBER update_ NOTIFY changed)
    Q_PROPERTY(QString prodKeys MEMBER prodKeys_ NOTIFY changed)
    Q_PROPERTY(QString titleKeys MEMBER titleKeys_ NOTIFY changed)
    Q_PROPERTY(bool complete READ complete NOTIFY changed)
public:
    using QObject::QObject;
    QString base_, update_, prodKeys_, titleKeys_;
    bool complete() const { return !base_.isEmpty() && !update_.isEmpty() && !prodKeys_.isEmpty(); }
    /// What a folder holds, the way inkypaint.sh prepare finds it: the NSPs by their title IDs, the keys by name, in
    /// the folder or one level down
    Q_INVOKABLE void scan(const QString& folder);
    /// One file (a path or a file: URL), wherever it fits
    Q_INVOKABLE void take(const QString& file);
    Q_INVOKABLE void setSlot(const QString& slot, const QString& file);
signals:
    void changed();
};

/// Extracts the game with its update and recompiles it, by running the bundled runtime as inkypaint.sh prepare does,
/// and follows its progress from what it prints (the Mac launcher's Preparer)
class Preparer : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString phase READ phase NOTIFY changed) // idle, extracting, recompiling, done, failed
    Q_PROPERTY(QString failure READ failure NOTIFY changed)
    Q_PROPERTY(double fraction READ fraction NOTIFY changed)
    Q_PROPERTY(QString detail READ detail NOTIFY changed)
    Q_PROPERTY(QStringList log READ log NOTIFY logChanged)
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(QString started READ started NOTIFY changed)
    Q_PROPERTY(bool installingDlc READ installingDlc NOTIFY changed)
public:
    explicit Preparer(QObject* parent = nullptr);
    QString phase() const { return phase_; }
    QString failure() const { return failure_; }
    double fraction() const { return 0.25 * extract_ + 0.75 * compile_; } // the recompile is most of it
    QString detail() const { return detail_; }
    QStringList log() const { return log_; }
    bool running() const { return phase_ == "extracting" || phase_ == "recompiling" || phase_ == "installingDlc"; }
    bool installingDlc() const { return dlc_; }
    QString started() const;
    /// Parallel compiles: two with 8 GB or less, which otherwise swaps; more with more memory
    static int jobs();

    void start(const GameSources* sources, const GamePaths& paths);
    void installDlc(const QString& file, const GameSources* sources, const GamePaths& paths);
    /// Several packages in turn: one install each, the next started when the one before has finished
    void installDlcs(const QStringList& files, const GameSources* sources, const GamePaths& paths);
    Q_INVOKABLE void retryDlc();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void reset();

signals:
    void changed();
    void logChanged();
    void finished();

private:
    void step();
    void run(const QStringList& args);
    void consume(const QByteArray& bytes);
    void parse(const QString& row);
    void append(const QString& row);
    void fail(const QString& why);

    QString phase_ = "idle", failure_, detail_;
    double extract_ = 0, compile_ = 0;
    QStringList log_;
    QDateTime started_;
    QProcess* process_ = nullptr;
    QString partial_;
    bool cancelled_ = false;
    bool dlc_ = false;
    QStringList dlcQueue_; // the packages still to install after the one running
    GamePaths paths_;
    QStringList extractArgs_;
};
