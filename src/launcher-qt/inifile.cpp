// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
#include "inifile.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

IniFile IniFile::load(const QString& path) {
    IniFile f;
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        QString text = QString::fromUtf8(file.readAll());
        text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        f.lines_ = text.split(QLatin1Char('\n'));
    } else {
        f.lines_ = {QString()};
    }
    return f;
}

bool IniFile::save(const QString& path, QString* error) const {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path); // written beside it and renamed over it: a crash never leaves half a file
    if (!file.open(QIODevice::WriteOnly) || file.write(lines_.join(QLatin1Char('\n')).toUtf8()) < 0 || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

int IniFile::index(const QString& key) const {
    for (int i = 0; i < lines_.size(); i++) {
        QString t = lines_[i].trimmed();
        int eq = t.indexOf(QLatin1Char('='));
        if (t.startsWith(QLatin1Char(';')) || eq < 0) continue;
        if (t.left(eq).trimmed() == key) return i;
    }
    return -1;
}

QString IniFile::value(const QString& key, bool* found) const {
    int i = index(key);
    if (found) *found = i >= 0;
    if (i < 0) return QString();
    return lines_[i].mid(lines_[i].indexOf(QLatin1Char('=')) + 1).trimmed();
}

void IniFile::set(const QString& key, const QString& value, const QString& section) {
    QString line = key + QLatin1Char('=') + value;
    int i = index(key);
    if (i >= 0) {
        lines_[i] = line;
        return;
    }
    QString header = QLatin1Char('[') + section + QLatin1Char(']');
    for (int h = 0; h < lines_.size(); h++) {
        if (lines_[h].trimmed() != header) continue;
        int at = h + 1; // after the section's last key, before the next section
        for (int j = h + 1; j < lines_.size() && !lines_[j].trimmed().startsWith(QLatin1Char('[')); j++)
            if (!lines_[j].trimmed().isEmpty()) at = j + 1;
        lines_.insert(at, line);
        return;
    }
    if (!lines_.isEmpty() && !lines_.last().isEmpty()) lines_.append(QString());
    lines_.append(header);
    lines_.append(line);
}
