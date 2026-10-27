// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
#pragma once
#include <QString>
#include <QStringList>

/// The game's mods on disk, as the runtime reads them (src/common/utilities/mods.c): each a folder of
/// <game>_data/mods/, with a romfs/ laid over the game's files when it starts, and an exefs/ (replacement modules or
/// .ips/.pchtxt patches) that only a recompile puts into the game's code. QtCore only, so tests can build it alone
namespace ModFiles {
/// The mods in folder, sorted as the runtime lays them (by name, byte order): no files, hidden folders or links
QStringList list(const QString& folder);
/// mods_off= as a list, and back (names with a / between two)
bool isOff(const QString& off, const QString& name);
QString setOff(const QString& off, const QString& name, bool isOff);
/// What a mod brings: files over the game's RomFS, and code the recompile has to take
bool hasRomfs(const QString& mod);
bool hasCode(const QString& mod);
/// snx_mods_stamp's value for the mods played: 16 hex digits, "" when none of them changes the code. The recompile
/// writes it into the game's <id>.abi after " mods ", so a difference means the code mods changed since
QString stamp(const QString& folder, const QString& off);
/// The stamp the game was last recompiled with ("" for none, or for a game not recompiled yet)
QString recorded(const QString& abiFile);

/// Where a download keeps its mods: every folder under base that holds a romfs/ or an exefs/ (the folder itself,
/// ModName/romfs, atmosphere/contents/<title ID>/romfs...), shallowest first, none inside another
QStringList roots(const QString& base);
/// A mod's folder name from a download's: no path separators, no leading dot, the title ID folder replaced by
/// fallback, and unused in folder (" 2", " 3"... added)
QString freeName(const QString& folder, const QString& wanted, const QString& fallback);
/// Copies root's romfs/ and exefs/ (whatever case they are named in) into the new mod folder to, through a hidden
/// folder renamed at the end, so the runtime never sees half a mod. False with *error set when it can't
bool install(const QString& root, const QString& to, QString* error);
} // namespace ModFiles
