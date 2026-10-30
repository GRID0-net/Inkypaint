// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
#pragma once
#include "inifile.h"
#include <QObject>
#include <QVariantList>

/// The game's .ini, read and written as settings change, and the settings the launcher shows (the Mac launcher's
/// SettingsCatalog). The runtime makes the file at the game's first start; until then the launcher writes the keys
/// it sets into a new one, which the runtime fills in.
class GameSettings : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList groups READ groups CONSTANT)
    Q_PROPERTY(bool exists READ exists NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(int revision READ revision NOTIFY changed) // QML bindings that call value() depend on it

public:
    explicit GameSettings(QObject* parent = nullptr);
    void setPath(const QString& path);
    QVariantList groups() const;
    bool exists() const;
    QString error() const { return error_; }
    int revision() const { return revision_; }

    Q_INVOKABLE QString value(const QString& key) const;
    Q_INVOKABLE void set(const QString& key, const QString& value);
    Q_INVOKABLE QString label(const QString& key) const; // the label a choice shows (Play's summary)
    Q_INVOKABLE QVariantList adapters() const;           // the LAN adapter choices, listed now
    // a key event's SDL name, as key_<control> holds it (keynames.h)
    Q_INVOKABLE QString keyName(int key, quint32 nativeScanCode, int modifiers) const;
    Q_INVOKABLE QString controllerValue(const QString& control) const;
    Q_INVOKABLE void setControllerMapping(const QString& control, const QString& target);
    Q_INVOKABLE void reload();
    Q_INVOKABLE void resetAll();
    Q_INVOKABLE void applyGraphicsPreset(const QString& name);
    /// A key the catalog doesn't show (the online settings), in section when the file doesn't have it yet
    void setRaw(const QString& key, const QString& value, const QString& section);
    QString raw(const QString& key, bool* found = nullptr) const { return file_.value(key, found); }

signals:
    void changed();

private:
    QVariantMap def(const QString& key) const;
    void save();
    QString path_;
    IniFile file_;
    QString error_;
    int revision_ = 0;
};
