// This file Copyright (c) Transmission contributors.
// Portable configuration discovery for the Qt client.
#pragma once

#include <QString>

namespace trqt::portable
{
struct Config
{
    QString directory;
    bool enabled = false;
};

// Portable mode is enabled by --portable or portable.dat beside the executable.
// An explicit config directory overrides the portable root but does not disable portable isolation.
[[nodiscard]] Config resolve(QString const& executable_path, QString const& explicit_config_dir, bool portable_flag);

// Import files that do not already exist in the destination. Never writes to the source profile.
void importOfficialConfig(QString const& official_directory, QString const& portable_directory);
} // namespace trqt::portable
