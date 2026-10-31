// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
#include "gamesettings.h"
#include "keynames.h"
#include <QFileInfo>
#include <QMap>
#include <QNetworkInterface>

namespace {
QVariantMap option(const QString& value, const QString& label) { return {{"value", value}, {"label", label}}; }
QVariantMap setting(const QString& key, const QString& section, const QString& title, const QString& detail,
                    const QString& kind, const QString& def, bool nextLaunch, const QVariantList& options = {}) {
    return {{"key", key},   {"section", section}, {"title", title},           {"detail", detail},
            {"kind", kind}, {"default", def},     {"nextLaunch", nextLaunch}, {"options", options}};
}
// a control's keys (key_<id>, kbm.c): empty for the game's own default, which shows as fallback
QVariantMap key(const QString& id, const QString& title, const QString& fallback) {
    QVariantMap m = setting("key_" + id, "Keyboard and mouse", title, "", "key", "", false);
    m.insert("fallback", fallback);
    return m;
}
// a control's keys in the game's menus (menu_key_<id>, kbm.c)
QVariantMap menuKey(const QString& id, const QString& title, const QString& fallback) {
    QVariantMap m = setting("menu_key_" + id, "Keyboard and mouse", title, "", "key", "", false);
    m.insert("fallback", fallback);
    return m;
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
                  {setting("resolution", "Graphics", "Resolution", "Scale the 3D scene, keeping the interface at its normal resolution. 1× follows the game.",
                           "choice", "1", true,
                           {option("0.25", "0.25×"), option("0.5", "0.5×"), option("0.75", "0.75×"), option("1", "1× (console)"),
                            option("1.5", "1.5×"), option("2", "2×"), option("3", "3×"), option("4", "4×")}),
                   setting("fps_limit", "Performance", "Frame rate", "F4 turns the limit off and on while you play.",
                           "choice", "game", false,
                           {option("game", "As the game asks (60)"), option("30", "30 FPS"), option("100", "100 FPS"), option("120", "120 FPS"),
                            option("0", "Unlimited")}),
                   setting("smooth_motion", "Performance", "Smooth motion (experimental)",
                           "Experimental interpolation with 60 Hz game logic. Uses the frame rate chosen above when it exceeds "
                           "60 FPS; otherwise targets 120. Requires GPU headroom.",
                           "toggle", "0", true),
                   setting("vsync", "Graphics", "VSync", "Waits for the display: no tearing.", "toggle", "1", false),
                   setting("anisotropy", "Graphics", "Texture filtering", "Sharper textures at a glancing angle.", "choice",
                           "0", true, {option("0", "As the game asks"), option("1", "Off"), option("2", "2×"), option("4", "4×"), option("8", "8×"), option("16", "16×")}),
                   setting("shadows", "Graphics", "Shadows", "Disable shadow maps and shadow post-processing in the engine.", "toggle", "1", true),
                   setting("bloom", "Graphics", "Bloom", "Bright lights glow.", "toggle", "1", true),
                   setting("depth_of_field", "Graphics", "Depth of field", "The game's focus blur.", "toggle", "1", true),
                   setting("vignette", "Graphics", "Vignette", "The game's vignette pass.", "toggle", "1", true),
                   setting("fog", "Graphics", "Fog", "Depth and height fog.", "toggle", "1", true),
                   setting("dynamic_resolution", "Graphics", "Dynamic resolution", "Adjust scene quality to completed GPU work. Resolution is the maximum scale.", "toggle", "0", true),
                   setting("dynamic_resolution_min", "Graphics", "Minimum render scale", "The lower limit for dynamic resolution.", "choice", "0.5", true,
                           {option("0.25", "0.25×"), option("0.5", "0.5×"), option("0.625", "0.625×"), option("0.75", "0.75×"), option("1", "1×")}),
                   setting("dynamic_resolution_fps", "Graphics", "Dynamic resolution target", "GPU frame budget. Does not change the game's speed or frame-rate limit.", "choice", "60", true,
                           {option("30", "30 FPS"), option("60", "60 FPS"), option("90", "90 FPS"), option("120", "120 FPS")}),
                   setting("texture_lod_bias", "Graphics", "Texture detail", "Coarser mipmaps reduce sampling detail. Texture allocations stay the same.", "choice", "0", true,
                           {option("0", "Game"), option("1", "Reduced"), option("2", "Low"), option("3", "Lowest")}),
                   setting("presentation_filter", "Graphics", "Window scaling filter", "Resizing the final image. The game upscales the scene separately.", "choice", "1", false,
                           {option("1", "Linear"), option("0", "Nearest")})}),
            group("Controls", "controls", {
                setting("keyboard_mouse_enabled", "Keyboard and mouse", "Keyboard and mouse input",
                        "Controller activity takes priority. Keyboard and mouse resume after the controller is idle.", "toggle", "1", false),
                setting("native_mouse_camera", "Keyboard and mouse", "Experimental mouse camera",
                        "The mouse turns the camera directly, like a PC game, and the game's motion controls are not "
                        "needed. Turn it off to use the regular mouse aim below, which works like motion controls.",
                        "toggle", "0", false),
                setting("gamepad_menu_layout", "Controllers", "Xbox/PlayStation layout in menus",
                        "Bottom button confirms in menus and activates visible A-button interaction prompts. Gameplay restores its own layout when the prompt disappears. Prompts keep Nintendo labels.", "toggle", "0", false)}),
            group("Controller input", "controls",
                  {setting("gamepad_deadzone", "Controllers", "Stick dead zone", "Ignore small stick movements around the center.", "choice", "20", false,
                           {option("0", "0%"), option("5", "5%"), option("10", "10%"), option("15", "15%"), option("20", "20%"), option("25", "25%"), option("30", "30%"), option("40", "40%")}),
                   setting("gamepad_rumble", "Controllers", "Rumble",
                           "How strongly your controller vibrates when the game asks. Off turns it off.", "choice", "100",
                           false,
                           {option("0", "Off"), option("40", "Light"), option("70", "Medium"), option("100", "Full")}),
                   setting("gamepad_map", "Controllers", "Controller buttons", "Gameplay mappings. The optional menu layout swaps A/B and X/Y in menus.", "controller_map", "", false)}),
            group("Keyboard and mouse", "keyboard",
                  {setting("mouse_aim", "Keyboard and mouse", "Mouse aim",
                           "The regular way to aim with the mouse: click in the game's window and it aims the way motion "
                           "controls do (turn the game's motion controls on). Left Alt lets the mouse go, and the game's "
                           "menu does too. The experimental mouse camera replaces this while it is on.",
                           "toggle", "1", false),
                   setting("mouse_sensitivity", "Keyboard and mouse", "Mouse sensitivity",
                           "How far the mouse turns the aim. The game's own motion sensitivity applies on top.", "choice",
                           "100", false,
                           {option("25", "25%"), option("50", "50%"), option("75", "75%"), option("100", "100%"),
                            option("125", "125%"), option("150", "150%"), option("200", "200%"), option("300", "300%"),
                            option("400", "400%")}),
                   setting("mouse_sensitivity_y", "Keyboard and mouse", "Vertical sensitivity",
                           "The up and down aim, compared to left and right.", "choice", "100", false,
                           {option("50", "50%"), option("75", "75%"), option("100", "Same"), option("125", "125%"),
                            option("150", "150%")}),
                   setting("mouse_invert_y", "Keyboard and mouse", "Invert vertical aim", "", "toggle", "0", false),
                   key("ls_up", "Move forward", "W"), key("ls_down", "Move back", "S"), key("ls_left", "Move left", "A"),
                   key("ls_right", "Move right", "D"), key("zr", "Shoot (ZR)", "Mouse Left"),
                   key("r", "Sub weapon (R)", "Mouse Right"), key("zl", "Swim (ZL)", "Left Shift"),
                   key("b", "Jump (B)", "Space, Backspace"), key("r3", "Special weapon (R stick)", "E"),
                   key("x", "Map (X)", "Tab"), key("y", "Camera behind you (Y)", "R"), key("a", "A", "F, Return"),
                   key("l", "L", "Q"), key("l3", "L stick click", "C"), key("up", "This way! (D-pad up)", "Up"),
                   key("down", "Booyah! (D-pad down)", "Down"), key("left", "D-pad left", "Left"),
                   key("right", "D-pad right", "Right"), key("plus", "Plus", "P"), key("minus", "Minus", "M"),
                   key("rs_up", "Camera up", "I"), key("rs_down", "Camera down", "K"), key("rs_left", "Camera left", "J"),
                   key("rs_right", "Camera right", "L"), key("cursor", "Free the mouse", "Left Alt")}),
            group("Keys in menus", "keyboard",
                  {setting("mouse_in_menus", "Keyboard and mouse", "Mouse in menus",
                           "The game's menus let the mouse go: point at a button and the menu's cursor goes there, "
                           "click to pick it. The keys below are the menus' own while one is up.",
                           "toggle", "1", false),
                   menuKey("a", "Pick (A)", "Return, Space, F"), menuKey("b", "Back (B)", "Backspace, Mouse Right"),
                   menuKey("l", "Tab left (L)", "Q"), menuKey("r", "Tab right (R)", "E"), menuKey("x", "X", "X, Tab"),
                   menuKey("y", "Y", "Y"), menuKey("zl", "ZL", "Z"), menuKey("zr", "ZR", "C"),
                   menuKey("up", "Up", "Up, W"), menuKey("down", "Down", "Down, S"), menuKey("left", "Left", "Left, A"),
                   menuKey("right", "Right", "Right, D"), menuKey("plus", "Plus", "P"), menuKey("minus", "Minus", "M"),
                   menuKey("l3", "L stick click", "Left Shift"), menuKey("r3", "R stick click", "Left Ctrl"),
                   menuKey("rs_up", "Right stick up", "I"), menuKey("rs_down", "Right stick down", "K"),
                   menuKey("rs_left", "Right stick left", "J"), menuKey("rs_right", "Right stick right", "L")}),
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
    if (key == "smooth_motion" && value == "0")
        file_.set("fps_limit", "game", "Performance");
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
QString GameSettings::keyName(int key, quint32 nativeScanCode, int modifiers) const {
    return sdlKeyName(key, nativeScanCode, Qt::KeyboardModifiers(modifiers));
}

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

void GameSettings::applyGraphicsPreset(const QString& name) {
    if (name != "Performance" && name != "Balanced" && name != "Quality") return;
    const bool fast = name == "Performance", high = name == "Quality";
    const QMap<QString, QString> values = {
        {"resolution", fast ? "0.75" : high ? "1.5" : "1"},
        {"dynamic_resolution", high ? "0" : "1"}, {"dynamic_resolution_min", fast ? "0.5" : "0.625"},
        {"dynamic_resolution_fps", "60"}, {"shadows", fast ? "0" : "1"}, {"bloom", fast ? "0" : "1"},
        {"depth_of_field", high ? "1" : "0"}, {"vignette", fast ? "0" : "1"}, {"fog", "1"},
        {"anisotropy", fast ? "1" : high ? "16" : "4"}, {"texture_lod_bias", fast ? "1" : "0"},
        {"presentation_filter", "1"}, {"smooth_motion", "0"}, {"fps_limit", "game"}, {"vsync", "1"}
    };
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        file_.set(it.key(), it.value(), def(it.key()).value("section").toString());
    save();
}

QString GameSettings::controllerValue(const QString& control) const {
    const QStringList keys = QStringLiteral("south east west north lb rb lt rt ls rs back start up down left right").split(' ');
    const QStringList defaults = QStringLiteral("B A Y X L R ZL ZR L3 R3 Minus Plus Up Down Left Right").split(' ');
    for (const auto& token : value("gamepad_map").simplified().split(' ', Qt::SkipEmptyParts))
        if (token.section(':', 0, 0).compare(control, Qt::CaseInsensitive) == 0) return token.section(':', 1);
    const int i = keys.indexOf(control);
    return i < 0 ? QStringLiteral("None") : defaults[i];
}
void GameSettings::setControllerMapping(const QString& control, const QString& target) {
    const QStringList keys = QStringLiteral("south east west north lb rb lt rt ls rs back start up down left right").split(' ');
    const QStringList buttons = QStringLiteral("A B X Y L R ZL ZR Plus Minus L3 R3 Up Down Left Right None").split(' ');
    if (!keys.contains(control) || !buttons.contains(target)) return;
    QStringList tokens;
    for (const auto& token : value("gamepad_map").simplified().split(' ', Qt::SkipEmptyParts))
        if (token.section(':', 0, 0).compare(control, Qt::CaseInsensitive) != 0) tokens.append(token);
    tokens.append(control + ':' + target);
    set("gamepad_map", tokens.join(' '));
}
