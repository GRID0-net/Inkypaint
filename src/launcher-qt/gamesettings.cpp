// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
#include "gamesettings.h"
#include <QFileInfo>
#include <QNetworkInterface>

namespace {
QVariantMap option(const QString& value, const QString& label) { return {{"value", value}, {"label", label}}; }
QVariantMap setting(const QString& key, const QString& section, const QString& title, const QString& detail,
                    const QString& kind, const QString& def, bool nextLaunch, const QVariantList& options = {}) {
    return {{"key", key},   {"section", section}, {"title", title},           {"detail", detail},
            {"kind", kind}, {"default", def},     {"nextLaunch", nextLaunch}, {"options", options}};
}
QVariantMap group(const QString& title, const QString& icon, const QVariantList& settings) {
    return {{"title", title}, {"icon", icon}, {"settings", settings}};
}

const QVariantList& catalog() {
    static const QVariantList groups = [] {
        QVariantList languages;
        const char* names[] = {"Japanese", "English (US)", "French", "German", "Italian", "Spanish", "Chinese", "Korean",
                               "Dutch", "Portuguese", "Russian", "Taiwanese", "English (UK)", "French (Canada)",
                               "Spanish (Latin America)", "Chinese (simplified)", "Chinese (traditional)",
                               "Portuguese (Brazil)"};
        for (int i = 0; i < int(sizeof names / sizeof *names); i++)
            languages.append(option(QString::number(i), QString::fromLatin1(names[i])));
        return QVariantList{
            group("Display", "display",
                  {setting("fullscreen", "Display", "Start in full screen", "F11 or Alt+Enter switches while you play.",
                           "toggle", "0", true),
                   setting("docked", "Display", "TV mode", "The console in its dock. Off plays as handheld, at 1280×720.",
                           "toggle", "1", true),
                   setting("adapt_aspect", "Graphics", "Fill wide windows",
                           "Widens the world to the window, the interface stays 16:9.", "toggle", "1", true),
                   setting("language", "Display", "Game language", "", "choice", "1", true, languages)}),
            group("Graphics", "graphics",
                  {setting("resolution", "Graphics", "Resolution", "The scale of the 3D scene. 1× is the console's 1080p.",
                           "choice", "1", true,
                           {option("0.5", "0.5× (fastest)"), option("0.75", "0.75×"), option("1", "1× (console)"),
                            option("1.5", "1.5×"), option("2", "2× (sharpest)")}),
                   setting("fps_limit", "Performance", "Frame rate", "F4 turns the limit off and on while you play.",
                           "choice", "game", false,
                           {option("game", "As the game asks (60)"), option("30", "30 FPS"), option("120", "120 FPS"),
                            option("0", "Unlimited")}),
                   setting("smooth_motion", "Performance", "Smooth motion (experimental)",
                           "Draws up to 120 frames a second while the game itself keeps its 60. Only shows when your "
                           "PC has GPU time to spare.",
                           "toggle", "0", true),
                   setting("vsync", "Graphics", "VSync", "Waits for the display: no tearing.", "toggle", "1", false),
                   setting("anisotropy", "Graphics", "Texture filtering", "Sharper textures at a glancing angle.", "choice",
                           "0", true, {option("0", "As the game asks"), option("4", "4×"), option("8", "8×"), option("16", "16×")}),
                   setting("shadows", "Graphics", "Shadows", "", "toggle", "1", false)}),
            group("Controls", "controls",
                  {setting("gamepad_rumble", "Controllers", "Rumble",
                           "How strongly your controller vibrates when the game asks. Off turns it off.", "choice", "100",
                           false,
                           {option("0", "Off"), option("40", "Light"), option("70", "Medium"), option("100", "Full")})}),
            group("LAN play", "network",
                  {setting("network", "Network", "Network", "Lets the game use your connection. LAN play and online play need it.",
                           "toggle", "0", false),
                   setting("lan_adapter", "Network", "LAN adapter",
                           "Where the game looks for LAN and local-play lobbies. Pick ZeroTier's adapter to play with friends "
                           "on your ZeroTier network as if you were on one LAN.",
                           "adapter", "", false)}),
            group("Shaders", "shaders",
                  {setting("async_pipelines", "Performance", "Compile shaders in the background",
                           "No pause when something new appears: it shows up a few frames late instead.", "toggle", "1", false),
                   setting("prepare_shaders", "Performance", "Shaders of earlier runs",
                           "Made before the game starts for no stutter, or while it plays to start sooner.", "choice", "2", true,
                           {option("2", "Before starting"), option("1", "While playing"), option("0", "Off")}),
                   setting("precise_float", "Performance", "Shader math",
                           "Fast lets the driver optimize the game's shaders. Exact rounds as the Switch does.", "choice",
                           "2", false,
                           {option("2", "Fast (recommended)"), option("1", "Exact"), option("0", "Fast everywhere")})}),
        };
    }();
    return groups;
}
} // namespace

GameSettings::GameSettings(QObject* parent) : QObject(parent) {}

void GameSettings::setPath(const QString& path) {
    path_ = path;
    reload();
}
QVariantList GameSettings::groups() const { return catalog(); }
bool GameSettings::exists() const { return QFileInfo::exists(path_); }

QVariantMap GameSettings::def(const QString& key) const {
    for (const QVariant& g : catalog())
        for (const QVariant& s : g.toMap().value("settings").toList())
            if (s.toMap().value("key").toString() == key) return s.toMap();
    return {};
}

QString GameSettings::value(const QString& key) const {
    bool found = false;
    QString v = file_.value(key, &found);
    return found ? v : def(key).value("default").toString();
}

void GameSettings::save() {
    QString why;
    error_ = file_.save(path_, &why) ? QString() : QStringLiteral("Couldn't save the settings: ") + why;
    revision_++;
    emit changed();
}

void GameSettings::set(const QString& key, const QString& value) {
    if (this->value(key) == value) return;
    file_.set(key, value, def(key).value("section").toString());
    save();
}

void GameSettings::setRaw(const QString& key, const QString& value, const QString& section) {
    bool found = false;
    if (file_.value(key, &found) == value && found) return;
    file_.set(key, value, section);
    save();
}

QString GameSettings::label(const QString& key) const {
    QVariantMap d = def(key);
    QString v = value(key), kind = d.value("kind").toString();
    if (kind == "toggle") return v == "0" ? QStringLiteral("Off") : QStringLiteral("On");
    const QVariantList options = kind == "adapter" ? adapters() : d.value("options").toList();
    for (const QVariant& o : options)
        if (o.toMap().value("value").toString() == v) return o.toMap().value("label").toString();
    return v;
}

/// The runtime finds the adapter by the same name (lan_adapter, net_iface.c): Windows' friendly names
/// ("ZeroTier One [8056c2e21c000001]", "Ethernet"), Linux's (zt..., eth0)
QVariantList GameSettings::adapters() const {
    QVariantList out{option("", "Automatic (the one the internet goes through)"),
                     option("virtual", "Virtual LAN when connected (ZeroTier, Hamachi, Radmin)")};
    for (const QNetworkInterface& i : QNetworkInterface::allInterfaces()) {
        if (!(i.flags() & QNetworkInterface::IsUp) || (i.flags() & QNetworkInterface::IsLoopBack)) continue;
        QString address;
        for (const QNetworkAddressEntry& e : i.addressEntries())
            if (e.ip().protocol() == QAbstractSocket::IPv4Protocol) address = e.ip().toString();
        if (address.isEmpty()) continue;
        QString name = i.humanReadableName(), lower = name.toLower();
        bool virtualLan = lower.contains("zerotier") || lower.contains("hamachi") || lower.contains("radmin") ||
                          lower.startsWith("zt") || lower.startsWith("feth") || lower.startsWith("ham");
        QString kind = virtualLan ? QStringLiteral(" (virtual LAN)")
                       : i.type() == QNetworkInterface::Wifi ? QStringLiteral(" (Wi-Fi)")
                       : i.type() == QNetworkInterface::Ethernet ? QStringLiteral(" (Ethernet)")
                                                                 : QString();
        out.append(option(name, name + ", " + address + kind));
    }
    out.append(option("all", "Every adapter (experimental)"));
    return out;
}

void GameSettings::reload() {
    file_ = IniFile::load(path_);
    revision_++;
    emit changed();
}

void GameSettings::resetAll() {
    for (const QVariant& g : catalog())
        for (const QVariant& s : g.toMap().value("settings").toList()) {
            QVariantMap m = s.toMap();
            if (m.value("key") == "network") continue; // online play's, which Online turns on and off
            file_.set(m.value("key").toString(), m.value("default").toString(), m.value("section").toString());
        }
    save();
}
