// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
#include "grid0patches.h"
#include "grid0.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <memory>

static const QString releases = "https://api.github.com/repos/GRID0-net/GRID0plus-cfw/releases?per_page=30";
static const QString downloads = "https://github.com/GRID0-net/GRID0plus-cfw/releases/download/";
static const QString prefix = "grid0plus-";

static QString modsFolder(const GamePaths& p) { return QDir(p.gameDir()).filePath(Game::titleID + "_data/mods"); }
static QString patchFile(const GamePaths& p, const QString& category) {
    return QDir(modsFolder(p)).filePath(prefix + category + "/exefs/" + Grid0::gameBuild + ".ips");
}
static QString sha256(const QByteArray& data) {
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

Grid0Patches::Grid0Patches(QObject* parent) : QObject(parent) {}

bool Grid0Patches::installed(const GamePaths& paths) {
    for (const QString& m : QDir(modsFolder(paths)).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        if (m.startsWith(prefix)) return true;
    return false;
}
bool Grid0Patches::compiledIn(const GamePaths& paths) {
    QFile stamp(QDir(paths.gameDir()).filePath(Game::titleID + ".abi"));
    return stamp.open(QIODevice::ReadOnly) && stamp.readAll().contains(" mods ");
}
bool Grid0Patches::needsRecompile(const GamePaths& paths) const {
    return installed(paths) && (QSettings().value("grid0PatchesChanged").toBool() || !compiledIn(paths));
}
void Grid0Patches::setChanged(bool on) {
    QSettings().setValue("grid0PatchesChanged", on);
    emit changed();
}
void Grid0Patches::recompiled() { setChanged(false); }

void Grid0Patches::fail(const QString& why) {
    status_ = "failed";
    failure_ = why;
    emit changed();
}

void Grid0Patches::get(const QString& url, std::function<void(const QByteArray&, const QString&)> done) {
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::UserAgentHeader, "inkypaint");
    request.setTransferTimeout(30000);
    QNetworkReply* reply = net_.get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, done] {
        reply->deleteLater();
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status != 200)
            return done({}, status ? QString("GitHub answered %1").arg(status) : reply->errorString());
        done(reply->readAll(), QString());
    });
}

namespace {
struct Entry {
    QString category, sha, asset;
};
}

void Grid0Patches::update(const GamePaths& paths) {
    if (status_ == "checking") return;
    status_ = "checking";
    emit changed();
    get(releases, [this, paths](const QByteArray& data, const QString& error) {
        if (!error.isEmpty()) return fail("Couldn't reach GRID0+'s patches: " + error);
        int best = -1;
        QString tag;
        for (const QJsonValue& v : QJsonDocument::fromJson(data).array()) { // the highest patches-<serial>
            QJsonObject r = v.toObject();
            QString t = r.value("tag_name").toString();
            bool ok = false;
            int serial = t.mid(8).toInt(&ok);
            if (!r.value("draft").toBool() && t.startsWith("patches-") && ok && serial > best) best = serial, tag = t;
        }
        if (tag.isEmpty()) return fail("No GRID0+ patch release was found.");
        get(downloads + tag + "/manifest.json", [this, paths, tag](const QByteArray& data, const QString& error) {
            if (!error.isEmpty()) return fail("Couldn't read the GRID0+ manifest: " + error);
            // the patches for emulators and this build; a malformed entry rejects the whole manifest
            static const QRegularExpression safe("^exefs_patches/([A-Za-z0-9_-][A-Za-z0-9_.-]*)/([A-Fa-f0-9]+)\\.ips$");
            static const QRegularExpression hex64("^[a-f0-9]{64}$");
            auto entries = std::make_shared<QList<Entry>>();
            for (const QJsonValue& v : QJsonDocument::fromJson(data).object().value("patches").toArray()) {
                QJsonObject p = v.toObject();
                if (!p.value("targets").toArray().contains(QJsonValue("emulator"))) continue;
                QString dest = p.value("dest").toString(), sha = p.value("sha256").toString(),
                        asset = p.value("asset").toString();
                QRegularExpressionMatch m = safe.match(dest);
                if (!m.hasMatch() || !hex64.match(sha).hasMatch() || asset != sha + ".ips")
                    return fail("The GRID0+ manifest has an entry inkypaint can't trust.");
                if (m.captured(2).toUpper() == Grid0::gameBuild) entries->append({m.captured(1), sha, asset});
            }
            if (entries->isEmpty()) return fail("The newest GRID0+ release has no patches for 11.3.0.");
            // what differs is downloaded first; nothing is written until every file has checked out
            auto downloaded = std::make_shared<QList<QPair<Entry, QByteArray>>>();
            auto next = std::make_shared<std::function<void(int)>>();
            *next = [this, paths, tag, entries, downloaded, next](int i) {
                if (i == entries->size()) {
                    for (const auto& d : *downloaded) {
                        QString path = patchFile(paths, d.first.category);
                        QDir().mkpath(QFileInfo(path).absolutePath());
                        QSaveFile f(path);
                        if (!f.open(QIODevice::WriteOnly) || f.write(d.second) < 0 || !f.commit())
                            return fail("Couldn't write the patch " + d.first.category + ".");
                    }
                    QSet<QString> wanted;
                    for (const Entry& e : *entries) wanted.insert(prefix + e.category);
                    bool removed = false;
                    for (const QString& m : QDir(modsFolder(paths)).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
                        if (m.startsWith(prefix) && !wanted.contains(m))
                            removed = QDir(QDir(modsFolder(paths)).filePath(m)).removeRecursively() || removed;
                    if (!downloaded->isEmpty() || removed) QSettings().setValue("grid0PatchesChanged", true);
                    status_ = "current";
                    tag_ = tag;
                    emit changed();
                    *next = nullptr; // the closure held itself
                    return;
                }
                const Entry e = entries->at(i);
                QFile have(patchFile(paths, e.category));
                if (have.open(QIODevice::ReadOnly) && sha256(have.readAll()) == e.sha) return (*next)(i + 1);
                get(downloads + tag + "/" + e.asset, [this, e, i, downloaded, next](const QByteArray& data, const QString& error) {
                    if (!error.isEmpty() || sha256(data) != e.sha)
                        return fail(e.category + " doesn't match the release's manifest. Nothing was changed.");
                    downloaded->append({e, data});
                    (*next)(i + 1);
                });
            };
            (*next)(0);
        });
    });
}
