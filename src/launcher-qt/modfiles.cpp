// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
#include "modfiles.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>

namespace {
const QChar separator = '/'; // SNX_MODS_SEPARATOR

/// strcmp's order, which the runtime sorts by
void byBytes(QStringList& names) {
    std::sort(names.begin(), names.end(), [](const QString& a, const QString& b) { return a.toUtf8() < b.toUtf8(); });
}
/// a folder's entries, hidden ones too (the runtime's snx_list_dir leaves out only . and ..)
QStringList entries(const QString& folder, QDir::Filters kind) {
    return QDir(folder).entryList(kind | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::NoSort);
}
bool isDir(const QString& path) {
    QFileInfo f(path);
    return f.isDir() && !f.isSymLink();
}
/// snx_mods_exefs_file: a module of the program (rtld, main, sdk, subsdk<n>) or main.npdm
bool exefsFile(const QString& name) {
    static const QRegularExpression module("^(rtld|main|sdk|main\\.npdm|subsdk[0-9]*)$");
    return name != "subsdk" && module.match(name).hasMatch();
}
bool patchFile(const QString& name) {
    return !name.startsWith('.') && (name.endsWith(".ips", Qt::CaseInsensitive) || name.endsWith(".pchtxt", Qt::CaseInsensitive));
}
/// the exefs/ folders of the mods played, in the order they are laid
QStringList playedExefs(const QString& folder, const QString& off) {
    QStringList out;
    for (const QString& mod : ModFiles::list(folder)) {
        QString exefs = QDir(folder).filePath(mod + "/exefs");
        if (!ModFiles::isOff(off, mod) && isDir(exefs)) out << exefs;
    }
    return out;
}
/// FNV-1a, 64 bits, over a file's name (with its terminating zero), its size (8 bytes, little-endian) and bytes
quint64 fnv(quint64 h, const QByteArray& data) {
    for (char c : data) h = (h ^ static_cast<unsigned char>(c)) * 0x100000001b3ull;
    return h;
}
quint64 hashFile(quint64 h, const QString& name, const QString& path) {
    QFile f(path);
    QByteArray data = f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    QByteArray size(8, '\0');
    for (int k = 0; k < 8; ++k) size[k] = static_cast<char>(static_cast<quint64>(data.size()) >> (8 * k));
    QByteArray named = name.toUtf8();
    named.append('\0');
    return fnv(fnv(fnv(h, named), size), data);
}
bool copyTree(const QString& fromPath, const QString& to, QString* error) {
    const QString from = QFileInfo(fromPath).absoluteFilePath();
    if (!QDir().mkpath(to)) {
        *error = "Couldn't make " + QDir::toNativeSeparators(to) + ".";
        return false;
    }
    QDirIterator it(from, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString path = it.next();
        QFileInfo f = it.fileInfo();
        QString target = QDir(to).filePath(QDir(from).relativeFilePath(path));
        if (f.isSymLink()) continue; // the runtime leaves links out
        if (f.isDir()) {
            if (!QDir().mkpath(target)) {
                *error = "Couldn't make " + QDir::toNativeSeparators(target) + ".";
                return false;
            }
        } else if (!QFile::copy(path, target)) {
            *error = "Couldn't copy " + f.fileName() + ". Is the disk full?";
            return false;
        }
    }
    return true;
}
} // namespace

QStringList ModFiles::list(const QString& folder) {
    QStringList out;
    for (const QString& name : entries(folder, QDir::Dirs))
        if (!name.startsWith('.') && isDir(QDir(folder).filePath(name))) out << name;
    byBytes(out);
    return out;
}

bool ModFiles::isOff(const QString& off, const QString& name) { return off.split(separator).contains(name); }
QString ModFiles::setOff(const QString& off, const QString& name, bool isOff) {
    QStringList names = off.split(separator, Qt::SkipEmptyParts);
    names.removeAll(name);
    if (isOff) names << name;
    return names.join(separator);
}

bool ModFiles::hasRomfs(const QString& mod) { return isDir(QDir(mod).filePath("romfs")); }
bool ModFiles::hasCode(const QString& mod) {
    QString exefs = QDir(mod).filePath("exefs");
    if (!isDir(exefs)) return false;
    for (const QString& name : entries(exefs, QDir::Files))
        if (exefsFile(name) || patchFile(name)) return true;
    return false;
}

/// snx_mods_stamp: the program's files from the mods (each from the last mod laid that has it, by name), then every
/// patch, mod by mod and by name in each
QString ModFiles::stamp(const QString& folder, const QString& off) {
    const QStringList exefs = playedExefs(folder, off);
    QStringList names, paths;
    for (const QString& dir : exefs)
        for (const QString& name : entries(dir, QDir::Files)) {
            QString path = QDir(dir).filePath(name);
            if (!exefsFile(name) || !QFileInfo(path).isFile()) continue;
            int at = names.indexOf(name);
            if (at >= 0) paths[at] = path;
            else names << name, paths << path;
        }
    QList<int> order(names.size());
    for (int i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](int a, int b) { return names[a].toUtf8() < names[b].toUtf8(); });
    quint64 h = 0xcbf29ce484222325ull;
    int count = 0;
    for (int i : order) h = hashFile(h, names[i], paths[i]), ++count;
    for (const QString& dir : exefs) {
        QStringList files = entries(dir, QDir::Files | QDir::Dirs);
        byBytes(files);
        for (const QString& name : files) {
            QString path = QDir(dir).filePath(name);
            if (!patchFile(name) || !QFileInfo(path).isFile()) continue;
            h = hashFile(h, name, path), ++count;
        }
    }
    return count ? QString::asprintf("%016llx", static_cast<unsigned long long>(h)) : QString();
}

QString ModFiles::recorded(const QString& abiFile) {
    QFile f(abiFile);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QByteArray text = f.readAll();
    int at = text.indexOf(" mods ");
    return at < 0 ? QString() : QString::fromUtf8(text.mid(at + 6));
}

QStringList ModFiles::roots(const QString& base) {
    QStringList found, queue{base};
    QList<int> depth{0};
    for (int i = 0; i < queue.size(); ++i) {
        QStringList children = entries(queue[i], QDir::Dirs);
        bool root = false;
        for (const QString& c : children)
            if (!c.compare("romfs", Qt::CaseInsensitive) || !c.compare("exefs", Qt::CaseInsensitive)) root = true;
        if (root) {
            found << queue[i];
            continue;
        }
        if (depth[i] >= 6) continue;
        byBytes(children);
        for (const QString& c : children) {
            QString path = QDir(queue[i]).filePath(c);
            if (c == "__MACOSX" || c.startsWith('.') || !isDir(path)) continue;
            queue << path;
            depth << depth[i] + 1;
        }
    }
    return found;
}

QString ModFiles::freeName(const QString& folder, const QString& wanted, const QString& fallback) {
    static const QRegularExpression titleId("^[0-9A-Fa-f]{16}$"), unsafe("[/\\\\:*?\"<>|]");
    QString name = wanted;
    if (titleId.match(name).hasMatch() || !name.compare("romfs", Qt::CaseInsensitive) ||
        !name.compare("contents", Qt::CaseInsensitive) || !name.compare("atmosphere", Qt::CaseInsensitive))
        name = fallback;
    name.replace(unsafe, "-");
    while (name.startsWith('.')) name.remove(0, 1);
    name = name.trimmed();
    if (name.isEmpty()) name = "Mod";
    QString free = name;
    for (int n = 2; QFileInfo::exists(QDir(folder).filePath(free)); ++n) free = name + " " + QString::number(n);
    return free;
}

bool ModFiles::install(const QString& root, const QString& to, QString* error) {
    QFileInfo target(to);
    QString staging = target.dir().filePath(".installing-" + target.fileName());
    QDir(staging).removeRecursively();
    bool ok = QDir().mkpath(staging);
    if (!ok) *error = "Couldn't make the mod's folder in " + QDir::toNativeSeparators(target.path()) + ".";
    for (const QString& part : entries(root, QDir::Dirs)) {
        QString lower = part.toLower();
        if (!ok || (lower != "romfs" && lower != "exefs")) continue;
        ok = copyTree(QDir(root).filePath(part), QDir(staging).filePath(lower), error);
    }
    if (ok && !QDir().rename(staging, to)) {
        ok = false;
        *error = "Couldn't put the mod in place.";
    }
    if (!ok) QDir(staging).removeRecursively();
    return ok;
}
