// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <QString>

namespace tr::portable {

// Remap paths in settings.json (download-dir, incomplete-dir, watch-dir)
// and disable dangerous automation (watch-dir, scripts)
bool migrateSettings(const QString& settingsPath);

// Remap paths in all .resume files (destination, incomplete-dir)
bool migrateResumeFiles(const QString& resumeDir);

} // namespace tr::portable