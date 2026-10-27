// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
#include "preparer.h"
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QThread>
#include <QUrl>
#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <unistd.h>
#endif

static QString localPath(const QString& file) {
    QUrl u(file);
    return u.isLocalFile() ? u.toLocalFile() : file;
}

void GameSources::take(const QString& file) {
    QString path = localPath(file);
    QFileInfo f(path);
    QString name = f.fileName().toLower();
    if (f.isDir()) {
        scan(path);
        return;
    }
    if (name == "prod.keys") prodKeys_ = path;
    else if (name == "title.keys") titleKeys_ = path;
    else if (f.suffix().toLower() == "nsp") {
        if (name.contains(Game::updateID.toLower()) || name.contains("[upd]")) update_ = path;
        else if (name.contains(Game::titleID.toLower()) || name.contains("[base]")) base_ = path;
    } else return;
    emit changed();
}

void GameSources::scan(const QString& folder) {
    QDir dir(localPath(folder));
    for (const QFileInfo& f : dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (f.isDir()) {
            for (const QFileInfo& g : QDir(f.filePath()).entryInfoList(QDir::Files)) take(g.filePath());
        } else {
            take(f.filePath());
        }
    }
}

void GameSources::setSlot(const QString& slot, const QString& file) {
    QString path = localPath(file);
    if (slot == "base") base_ = path;
    else if (slot == "update") update_ = path;
    else if (slot == "prodKeys") prodKeys_ = path;
    else if (slot == "titleKeys") titleKeys_ = path;
    emit changed();
}

Preparer::Preparer(QObject* parent) : QObject(parent) {}

QString Preparer::started() const { return started_.isValid() ? started_.toString("HH:mm") : QString(); }

int Preparer::jobs() {
    qint64 gb = 0;
#ifdef Q_OS_WIN
    MEMORYSTATUSEX m;
    m.dwLength = sizeof m;
    if (GlobalMemoryStatusEx(&m)) gb = qint64(m.ullTotalPhys >> 30);
#else
    long pages = sysconf(_SC_PHYS_PAGES), size = sysconf(_SC_PAGESIZE);
    if (pages > 0 && size > 0) gb = (qint64(pages) * size) >> 30;
#endif
    /* a compile takes up to about 2 GB (giant functions are compiled at O1, which used to take the most), and the
       system keeps 4: two on 8 GB, six on 16 GB, every thread of an 8-core, 16-thread CPU from 36 GB */
    if (gb <= 8) return 2;
    return int(qBound<qint64>(2, (gb - 4) / 2, qint64(qMax(2, QThread::idealThreadCount()))));
}

void Preparer::start(const GameSources* sources, const GamePaths& paths) {
    if (running()) return;
    if (!Bundled::isComplete()) {
        fail("This copy of inkypaint doesn't contain the runtime (scarletnx). Reinstall inkypaint.");
        return;
    }
    cancelled_ = false;
    log_.clear();
    emit logChanged();
    started_ = QDateTime::currentDateTime();
    extract_ = compile_ = 0;
    paths_ = paths;
    QDir().mkpath(paths.data);
    extractArgs_.clear();
    if (paths.isExtracted()) {
        append("Already extracted: " + displayPath(paths.work()));
        extract_ = 1;
    } else {
        if (!sources->complete()) {
            fail("Choose the game, its update and your prod.keys first.");
            return;
        }
        extractArgs_ = {"--data", paths.data, "-x", sources->base_, "--update", sources->update_, "--keys",
                        sources->prodKeys_};
        if (!sources->titleKeys_.isEmpty()) extractArgs_ << "--title-keys" << sources->titleKeys_;
        extractArgs_ << "--out" << paths.work();
    }
    phase_ = extractArgs_.isEmpty() ? "recompiling" : "extracting";
    emit changed();
    step();
}

/// The next command: the extraction (unless it is there), then the recompile
void Preparer::step() {
    if (phase_ == "extracting") {
        detail_ = "Reading the game";
        run(extractArgs_);
    } else {
        detail_ = "Translating the game for this PC";
        run({"--data", paths_.data, "-r", paths_.work(), "--outdir", paths_.recompiled(), "--jobs",
             QString::number(jobs()), "-O", "2"});
    }
    emit changed();
}

void Preparer::run(const QStringList& args) {
    process_ = new QProcess(this);
    process_->setProgram(Bundled::runtime());
    process_->setArguments(args);
    process_->setProcessEnvironment(Bundled::environment());
    process_->setProcessChannelMode(QProcess::MergedChannels);
    append("> scarletnx " + args.join(' '));
    connect(process_, &QProcess::readyReadStandardOutput, this, [this] { consume(process_->readAllStandardOutput()); });
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) fail("The runtime couldn't start: " + process_->errorString());
    });
    connect(process_, &QProcess::finished, this, [this](int status, QProcess::ExitStatus how) {
        consume(process_->readAllStandardOutput());
        if (!partial_.trimmed().isEmpty()) parse(partial_);
        partial_.clear();
        process_->deleteLater();
        process_ = nullptr;
        if (cancelled_) {
            fail("Stopped. Preparing again picks up where it stopped.");
            return;
        }
        if (status != 0 || how != QProcess::NormalExit) {
            QString last;
            for (int i = log_.size() - 1; i >= 0 && last.isEmpty(); i--)
                if (log_[i].contains("error", Qt::CaseInsensitive) || log_[i].contains("cannot", Qt::CaseInsensitive))
                    last = log_[i];
            fail(last.isEmpty() ? QString("The runtime stopped with status %1.").arg(status) : last);
            return;
        }
        if (phase_ == "extracting") {
            extract_ = 1;
            phase_ = "recompiling";
            step();
            return;
        }
        compile_ = 1;
        if (paths_.isPrepared()) {
            phase_ = "done";
            detail_ = "Done";
            emit changed();
            emit finished();
        } else {
            fail("The recompile ended without making the game. See the details.");
        }
    });
    process_->start();
}

void Preparer::cancel() {
    cancelled_ = true;
    if (process_) process_->kill();
}

void Preparer::reset() {
    if (running()) return;
    phase_ = "idle";
    failure_.clear();
    emit changed();
}

void Preparer::fail(const QString& why) {
    phase_ = "failed";
    failure_ = why;
    emit changed();
    emit finished();
}

/// The runtime's output in rows: "\r" ends the extraction's progress rows, which rewrite one line in a terminal
void Preparer::consume(const QByteArray& bytes) {
    partial_ += QString::fromUtf8(bytes);
    QStringList rows = partial_.split(QRegularExpression("[\r\n]"));
    partial_ = rows.takeLast();
    for (const QString& row : rows)
        if (!row.trimmed().isEmpty()) parse(row);
}

static QList<qint64> numbers(const QString& s) {
    QList<qint64> out;
    QRegularExpressionMatchIterator it = QRegularExpression("\\d+").globalMatch(s);
    while (it.hasNext()) out.append(it.next().captured().toLongLong());
    return out;
}

void Preparer::parse(const QString& row) {
    QString t = row.trimmed();
    QList<qint64> n = numbers(t);
    if (t.startsWith("extracted ") && n.size() >= 2 && n[1] > 0) { // "extracted 1229 of 3164 MB (38%)"
        extract_ = qMin(1.0, double(n[0]) / double(n[1]));
        detail_ = QString("Extracted %1 of %2 MB").arg(n[0]).arg(n[1]);
        emit changed();
        if (!log_.isEmpty() && log_.last().trimmed().startsWith("extracted ")) {
            log_.last() = row;
            emit logChanged();
            return;
        }
    } else if (t.startsWith("compiled block ") && n.size() >= 2 && n[1] > 0) { // "compiled block 37/246: code_36"
        compile_ = qMin(1.0, double(n[0]) / double(n[1]));
        detail_ = QString("Compiled %1 of %2 blocks").arg(n[0]).arg(n[1]);
        emit changed();
    } else if (t.startsWith("Built ")) {
        detail_ = "Done";
        emit changed();
    }
    append(row);
}

void Preparer::append(const QString& row) {
    log_.append(row);
    if (log_.size() > 400) log_.erase(log_.begin(), log_.begin() + (log_.size() - 400));
    emit logChanged();
}
