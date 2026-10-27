// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
#pragma once

// Import settings written before the launcher had an organization, without
// overwriting preferences already saved under its stable identity.
void initializeLauncherIdentity();
void migrateLauncherSettings();
