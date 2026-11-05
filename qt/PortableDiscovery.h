// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <string>
#include <vector>

namespace tr
{
namespace portable
{

struct HostInstall
{
    std::string config_dir;     // Where settings.json, resume/, torrents/ live
    std::string install_dir;    // Where transmission-qt.exe lives
    std::string type;           // "official", "msstore", "portable"
    std::string version;        // Version string if detectable
};

std::vector<HostInstall> discoverHostInstalls();

// Utility functions for path checking
bool dir_exists(std::string_view path);
bool file_exists(std::string_view path);

} // namespace portable
} // namespace tr
