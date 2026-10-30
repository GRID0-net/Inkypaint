// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
#include "keynames.h"
#include <QGuiApplication>
#include <QHash>

// SDL's scancodes are places on the keyboard, named as a US layout has them: the W a French player presses for
// forward is their Z key. So a key that has a place of its own (the letters, the digit row, the punctuation, the
// modifiers' left and right) is named by where it is, from the system's own scancode; the others by what they are
namespace {
// Windows: the scan code set 1 code Qt gives, 0x100 on an extended key
const QHash<quint32, QString>& windowsCodes() {
    static const QHash<quint32, QString> m = {
        {0x02, "1"}, {0x03, "2"}, {0x04, "3"}, {0x05, "4"}, {0x06, "5"}, {0x07, "6"}, {0x08, "7"}, {0x09, "8"},
        {0x0A, "9"}, {0x0B, "0"}, {0x0C, "-"}, {0x0D, "="}, {0x10, "Q"}, {0x11, "W"}, {0x12, "E"}, {0x13, "R"},
        {0x14, "T"}, {0x15, "Y"}, {0x16, "U"}, {0x17, "I"}, {0x18, "O"}, {0x19, "P"}, {0x1A, "["}, {0x1B, "]"},
        {0x1D, "Left Ctrl"}, {0x1E, "A"}, {0x1F, "S"}, {0x20, "D"}, {0x21, "F"}, {0x22, "G"}, {0x23, "H"},
        {0x24, "J"}, {0x25, "K"}, {0x26, "L"}, {0x27, ";"}, {0x28, "'"}, {0x29, "`"}, {0x2A, "Left Shift"},
        {0x2B, "\\"}, {0x2C, "Z"}, {0x2D, "X"}, {0x2E, "C"}, {0x2F, "V"}, {0x30, "B"}, {0x31, "N"}, {0x32, "M"},
        {0x33, ","}, {0x34, "."}, {0x35, "/"}, {0x36, "Right Shift"}, {0x38, "Left Alt"}, {0x11D, "Right Ctrl"},
        {0x138, "Right Alt"}, {0x15B, "Left GUI"}, {0x15C, "Right GUI"}};
    return m;
}
// X11 and Wayland: the XKB keycode, Linux's evdev code + 8
const QHash<quint32, QString>& xkbCodes() {
    static const QHash<quint32, QString> m = {
        {10, "1"}, {11, "2"}, {12, "3"}, {13, "4"}, {14, "5"}, {15, "6"}, {16, "7"}, {17, "8"}, {18, "9"},
        {19, "0"}, {20, "-"}, {21, "="}, {24, "Q"}, {25, "W"}, {26, "E"}, {27, "R"}, {28, "T"}, {29, "Y"},
        {30, "U"}, {31, "I"}, {32, "O"}, {33, "P"}, {34, "["}, {35, "]"}, {37, "Left Ctrl"}, {38, "A"},
        {39, "S"}, {40, "D"}, {41, "F"}, {42, "G"}, {43, "H"}, {44, "J"}, {45, "K"}, {46, "L"}, {47, ";"},
        {48, "'"}, {49, "`"}, {50, "Left Shift"}, {51, "\\"}, {52, "Z"}, {53, "X"}, {54, "C"}, {55, "V"},
        {56, "B"}, {57, "N"}, {58, "M"}, {59, ","}, {60, "."}, {61, "/"}, {62, "Right Shift"}, {64, "Left Alt"},
        {105, "Right Ctrl"}, {108, "Right Alt"}, {133, "Left GUI"}, {134, "Right GUI"}};
    return m;
}
QString byKey(int key, Qt::KeyboardModifiers mods) {
    const bool keypad = mods & Qt::KeypadModifier;
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) return QStringLiteral("F%1").arg(key - Qt::Key_F1 + 1);
    if (key >= Qt::Key_0 && key <= Qt::Key_9)
        return keypad ? QStringLiteral("Keypad %1").arg(key - Qt::Key_0) : QString(QChar(key));
    if (key >= Qt::Key_A && key <= Qt::Key_Z) return QString(QChar(key));
    if (keypad) {
        switch (key) {
        case Qt::Key_Enter: return "Keypad Enter";
        case Qt::Key_Plus: return "Keypad +";
        case Qt::Key_Minus: return "Keypad -";
        case Qt::Key_Asterisk: return "Keypad *";
        case Qt::Key_Slash: return "Keypad /";
        case Qt::Key_Period: return "Keypad .";
        case Qt::Key_Comma: return "Keypad ,";
        case Qt::Key_Equal: return "Keypad =";
        default: break;
        }
    }
    switch (key) {
    case Qt::Key_Space: return "Space";
    case Qt::Key_Return: return "Return";
    case Qt::Key_Enter: return "Keypad Enter";
    case Qt::Key_Tab:
    case Qt::Key_Backtab: return "Tab";
    case Qt::Key_Backspace: return "Backspace";
    case Qt::Key_Insert: return "Insert";
    case Qt::Key_Delete: return "Delete";
    case Qt::Key_Home: return "Home";
    case Qt::Key_End: return "End";
    case Qt::Key_PageUp: return "PageUp";
    case Qt::Key_PageDown: return "PageDown";
    case Qt::Key_Left: return "Left";
    case Qt::Key_Right: return "Right";
    case Qt::Key_Up: return "Up";
    case Qt::Key_Down: return "Down";
    case Qt::Key_CapsLock: return "CapsLock";
    case Qt::Key_ScrollLock: return "ScrollLock";
    case Qt::Key_NumLock: return "Numlock";
    case Qt::Key_Pause: return "Pause";
    case Qt::Key_Print: return "PrintScreen";
    case Qt::Key_Menu: return "Application";
    case Qt::Key_Shift: return "Left Shift";
    case Qt::Key_Control: return "Left Ctrl";
    case Qt::Key_Alt: return "Left Alt";
    case Qt::Key_Meta: return "Left GUI";
    case Qt::Key_AltGr: return "Right Alt";
    // the punctuation and what Shift makes of it, by the US key it is on
    case Qt::Key_Minus: case Qt::Key_Underscore: return "-";
    case Qt::Key_Equal: case Qt::Key_Plus: return "=";
    case Qt::Key_BracketLeft: case Qt::Key_BraceLeft: return "[";
    case Qt::Key_BracketRight: case Qt::Key_BraceRight: return "]";
    case Qt::Key_Backslash: case Qt::Key_Bar: return "\\";
    case Qt::Key_Semicolon: case Qt::Key_Colon: return ";";
    case Qt::Key_Apostrophe: case Qt::Key_QuoteDbl: return "'";
    case Qt::Key_QuoteLeft: case Qt::Key_AsciiTilde: return "`";
    case Qt::Key_Comma: case Qt::Key_Less: return ",";
    case Qt::Key_Period: case Qt::Key_Greater: return ".";
    case Qt::Key_Slash: case Qt::Key_Question: return "/";
    case Qt::Key_Exclam: return "1";
    case Qt::Key_At: return "2";
    case Qt::Key_NumberSign: return "3";
    case Qt::Key_Dollar: return "4";
    case Qt::Key_Percent: return "5";
    case Qt::Key_AsciiCircum: return "6";
    case Qt::Key_Ampersand: return "7";
    case Qt::Key_Asterisk: return "8";
    case Qt::Key_ParenLeft: return "9";
    case Qt::Key_ParenRight: return "0";
    default: return {};
    }
}
} // namespace

QString sdlKeyName(int key, quint32 nativeScanCode, Qt::KeyboardModifiers modifiers) {
    if (!(modifiers & Qt::KeypadModifier) && nativeScanCode) {
        const QString platform = QGuiApplication::platformName();
        const QHash<quint32, QString>& codes = platform == "windows" ? windowsCodes() : xkbCodes();
        if (platform == "windows" || platform == "xcb" || platform == "wayland") {
            auto it = codes.find(nativeScanCode);
            if (it != codes.end()) return it.value();
        }
    }
    return byKey(key, modifiers);
}
