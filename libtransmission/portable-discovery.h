#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "libtransmission/variant.h"

namespace tr
{

enum class InstallType : std::uint8_t
{
    Unknown,
    Official,
    Portable,
    MicrosoftStore,
    DaemonService
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

struct FileLockInfo
{
    std::string process_name;
    uint32_t process_id = 0;
    std::string file_path;
};

struct ImportResult
{
    bool success = false;

    std::vector<std::string> imported_torrents;
    std::vector<std::string> skipped_torrents;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    std::vector<FileLockInfo> locked_files;

    std::optional<tr_variant> merged_settings;
};

std::vector<DiscoveredInstall> discover_installations();

bool is_portable_mode(int argc, char* argv[], const std::string& exe_path);
std::string get_portable_root(const std::string& exe_path);
bool has_portable_marker(const std::string& exe_path);

ImportResult import_from_install(const DiscoveredInstall& install, const PortableOptions& options);

std::string drive_to_unc(const std::string& path);
std::string normalize_path(const std::string& path);
bool is_nas_path(const std::string& path);

} // namespace tr
