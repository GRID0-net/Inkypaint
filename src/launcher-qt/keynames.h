// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
#pragma once
#include <QString>
#include <Qt>

/// The SDL name of the key a key event is (W, Left Shift, Space), as the game's key_<control> settings name keys:
/// by where the key is for the keys that have a place (letters, digits, punctuation, modifiers), "" for none
QString sdlKeyName(int key, quint32 nativeScanCode, Qt::KeyboardModifiers modifiers);
