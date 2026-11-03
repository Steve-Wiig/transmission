// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstdint> // uint8_t
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "libtransmission/variant.h"

/**
 * @addtogroup portable Portable auto-discovery layer (Windows / MinGW builds)
 *
 * @{
 *
 * Self-discovering, relocatable Transmission builds on Windows:
 * - discovers existing official / daemon / Microsoft Store installs via the
 *   registry and well-known paths
 * - imports settings.json, resume/*.resume (with drive-letter → UNC remapping
 *   for NAS/SMB-mapped drives) and torrents/*.torrent files
 * - activates "portable mode" when a `--portable` flag is present or a
 *   `portable.dat` marker file sits next to the executable
 *
 * All functions are safe to call from any platform; on non-Windows hosts the
 * discovery results will simply be empty and drive_to_unc() returns its input.
 */

namespace tr
{

enum class InstallType : uint8_t
{
    Unknown,
    Official,
    Portable,
    MicrosoftStore,
    DaemonService,
};

struct DiscoveredInstall
{
    InstallType type = InstallType::Unknown;
    std::string install_root;
    std::string config_root;
    std::string version;
    std::string exe_path;
    bool has_settings = false;
    bool has_resume_dir = false;
    bool has_torrents_dir = false;
    std::vector<std::string> warnings;
};

struct PortableOptions
{
    bool portable_mode = false;
    std::string portable_root;
    bool remap_drive_to_unc = true;
    bool import_settings = true;
    bool import_resume = true;
    bool import_torrent_files = true;
    bool fail_fast = false;
};

struct ImportResult
{
    bool success = false;
    std::vector<std::string> imported_torrents;
    std::vector<std::string> skipped_torrents;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    std::optional<tr_variant> merged_settings;
};

/**
 * @brief Scan registry + well-known locations for Transmission installations.
 *
 * Results are sorted by preference: Portable > Official > DaemonService >
 * MicrosoftStore > Unknown. Never throws; per-source failures are logged as
 * warnings attached to the affected entry (or dropped silently if no entry).
 */
std::vector<DiscoveredInstall> discover_installations();

/**
 * @brief True when `--portable` appears in argv or a `portable.dat` marker
 * file exists next to the executable at `exe_path`.
 */
bool is_portable_mode(int argc, char* argv[], std::string_view exe_path);

/**
 * @brief The portable root: directory containing the executable, with any
 * trailing path separators removed.
 */
std::string get_portable_root(std::string_view exe_path);

/**
 * @brief True when `<exe dir>/portable.dat` exists.
 */
bool has_portable_marker(std::string_view exe_path);

/**
 * @brief Import settings / resume files / torrent metainfo from `install`
 * into the portable tree described by `options`.
 *
 * Graceful degradation is guaranteed: individual corrupt resume files or
 * invalid metainfo files are recorded in `ImportResult::skipped_torrents`
 * (with a warning) instead of aborting the import. Nothing outside the
 * portable root is ever written unless `options.portable_root` points there
 * explicitly.
 */
ImportResult import_from_install(DiscoveredInstall const& install, PortableOptions const& options);

/**
 * @brief If `path` starts with a mapped network drive letter ("Z:\\share\\x"),
 * rewrite the prefix to its UNC form ("\\\\server\\share\\x") using
 * WNetGetConnectionW(). Results are cached per-drive. On non-Windows builds,
 * or when the drive is not a network mapping, `path` is returned unchanged.
 */
std::string drive_to_unc(std::string_view path);

/**
 * @brief Normalize a path for internal storage: unify separators to '/',
 * squash duplicates, strip trailing separators (except roots), resolve "."
 * segments. Does NOT touch the case of drive letters' UNC prefixes.
 */
std::string normalize_path(std::string_view path);

/**
 * @brief True when `path` is a UNC path ("\\\\server\\share..."), a
 * "\\?\UNC\..." extended path, or lives on a mapped network drive.
 */
bool is_nas_path(std::string_view path);

/**
 * @brief Clear the internal drive→UNC cache (used by tests & after
 * `net use` changes).
 */
void invalidate_drive_cache();

} // namespace tr

/* @} */
