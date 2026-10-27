// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
#pragma once
#include <QString>
#include <QStringList>

/// The game's settings file as the runtime writes it: [Section] headers, key=value lines and ; comments. Only the lines
/// of the keys set change: comments, order and every other key stay as they are (src/launcher's IniFile)
class IniFile {
public:
    static IniFile load(const QString& path);
    bool save(const QString& path, QString* error = nullptr) const;
    QString value(const QString& key, bool* found = nullptr) const;
    /// Sets key, in section when the file doesn't have it yet (the section is added at the end if it is missing)
    void set(const QString& key, const QString& value, const QString& section);

private:
    int index(const QString& key) const;
    QStringList lines_;
};
