// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <string>
#include <vector>
#include <optional>

#include "PortableDiscovery.h"

namespace tr
{
namespace portable
{

struct FileLockInfo
{
    std::string process_name;
    uint32_t process_id = 0;
    std::string file_path;
};

struct ImportOptions
{
    bool import_settings = true;
    bool import_resume = true;
    bool import_torrent_files = true;
    bool remap_drive_to_unc = true;
    bool fail_fast = false;
};

struct ImportResult
{
    bool success = false;

    std::vector<std::string> imported_torrents;
    std::vector<std::string> skipped_torrents;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    std::vector<FileLockInfo> locked_files;
};

/// Check if any files in the source config directory are locked by running processes
/// Uses Windows Restart Manager (RmStartSession, RmRegisterResources, RmGetList)
std::vector<FileLockInfo> checkFileLocks(std::string const& config_dir);

/// Stage an import from a host installation to the portable directory
/// Returns ImportResult with details of what was copied/skipped/failed
ImportResult stageImport(HostInstall const& host_install, std::string const& portable_root, ImportOptions const& options);

/// Normalize a Windows path (replace backslashes, handle drive letters)
std::string normalize_path(std::string const& path);

/// Convert a drive-letter path to UNC if on a network share
std::string drive_to_unc(std::string const& path);

} // namespace portable
} // namespace tr
