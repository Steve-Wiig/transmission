// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cctype>
#include <cwctype>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <lm.h>
#include <shlobj.h>
#include <winreg.h>
#include <wnet.h>
#pragma comment(lib, "mpr.lib")
#pragma comment(lib, "netapi32.lib")
#endif

#include <fmt/format.h>

#include "libtransmission/benc.h"
#include "libtransmission/error.h"
#include "libtransmission/file-utils.h"
#include "libtransmission/file.h"
#include "libtransmission/log.h"
#include "libtransmission/portable-discovery.h"
#include "libtransmission/serializer.h"
#include "libtransmission/string-utils.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/utils.h"

using namespace std::literals;

namespace
{

// --- Drive to UNC cache ---
std::mutex g_drive_unc_mutex;
std::map<std::string, std::string> g_drive_unc_cache;

// --- Helper: Convert wide string to UTF-8 ---
std::string wstring_to_utf8(std::wstring_view wstr)
{
    if (wstr.empty())
        return {};

    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return {};

    std::string result;
    result.resize(size);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), result.data(), size, nullptr, nullptr);
    return result;
}

// --- Helper: Convert UTF-8 to wide string ---
std::wstring utf8_to_wstring(std::string_view str)
{
    if (str.empty())
        return {};

    int size = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
    if (size <= 0)
        return {};

    std::wstring result;
    result.resize(size);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), size);
    return result;
}

// --- Helper: Read registry value ---
std::optional<std::string> read_registry_string(HKEY root, std::wstring_view subkey, std::wstring_view value_name)
{
    HKEY key = nullptr;
    LSTATUS status = RegOpenKeyExW(root, subkey.data(), 0, KEY_READ | KEY_WOW64_64KEY, &key);
    if (status != ERROR_SUCCESS)
    {
        status = RegOpenKeyExW(root, subkey.data(), 0, KEY_READ | KEY_WOW64_32KEY, &key);
        if (status != ERROR_SUCCESS)
            return std::nullopt;
    }

    DWORD type = 0;
    DWORD size = 0;
    status = RegQueryValueExW(key, value_name.data(), nullptr, &type, nullptr, &size);
    if (status != ERROR_SUCCESS || type != REG_SZ || size == 0)
    {
        RegCloseKey(key);
        return std::nullopt;
    }

    std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1);
    status = RegQueryValueExW(key, value_name.data(), nullptr, &type, reinterpret_cast<LPBYTE>(buffer.data()), &size);
    RegCloseKey(key);

    if (status != ERROR_SUCCESS)
        return std::nullopt;

    return wstring_to_utf8(std::wstring_view{buffer.data()});
}

// --- Helper: Expand environment variables in path ---
std::string expand_env_vars(std::string_view path)
{
#ifdef _WIN32
    std::wstring wpath = utf8_to_wstring(path);
    wchar_t expanded[MAX_PATH * 4];
    DWORD result = ExpandEnvironmentStringsW(wpath.c_str(), expanded, MAX_PATH * 4);
    if (result == 0 || result > MAX_PATH * 4)
        return std::string(path);
    return wstring_to_utf8(std::wstring_view{expanded});
#else
    return std::string(path);
#endif
}

// --- Helper: Check if directory exists ---
bool dir_exists(std::string_view path)
{
    tr_error error;
    auto info = tr_sys_path_get_info(path, 0);
    return info && info->isFolder();
}

// --- Helper: Get Transmission version from exe ---
std::string get_exe_version(std::string_view exe_path)
{
#ifdef _WIN32
    std::wstring wexe = utf8_to_wstring(exe_path);
    DWORD handle = 0;
    DWORD size = GetFileVersionInfoSizeW(wexe.c_str(), &handle);
    if (size == 0)
        return {};

    std::vector<uint8_t> buffer(size);
    if (!GetFileVersionInfoW(wexe.c_str(), 0, size, buffer.data()))
        return {};

    VS_FIXEDFILEINFO* file_info = nullptr;
    UINT file_info_size = 0;
    if (!VerQueryValueW(buffer.data(), L"\\", reinterpret_cast<LPVOID*>(&file_info), &file_info_size))
        return {};

    if (file_info_size < sizeof(VS_FIXEDFILEINFO))
        return {};

    return fmt::format("{}.{}.{}",
        (file_info->dwFileVersionMS >> 16) & 0xFFFF,
        file_info->dwFileVersionMS & 0xFFFF,
        (file_info->dwFileVersionLS >> 16) & 0xFFFF);
#else
    (void)exe_path;
    return {};
#endif
}

// --- Helper: InstallType to string for logging ---
std::string_view install_type_to_string(tr::InstallType type)
{
    switch (type)
    {
        case tr::InstallType::Unknown: return "Unknown";
        case tr::InstallType::Official: return "Official";
        case tr::InstallType::Portable: return "Portable";
        case tr::InstallType::MicrosoftStore: return "MicrosoftStore";
        case tr::InstallType::DaemonService: return "DaemonService";
    }
    return "Unknown";
}

} // unnamed namespace

namespace tr
{

// --- drive_to_unc ---
std::string drive_to_unc(const std::string& path)
// --- drive_to_unc ---
std::string drive_to_unc(const std::string& path)
{
    if (path.size() < 2 || path[1] != ':')
        return path; // Not a drive letter path

    char drive_letter = std::toupper(static_cast<unsigned char>(path[0]));
    if (drive_letter < 'A' || drive_letter > 'Z')
        return path;

    std::string drive_key = std::string(1, drive_letter) + ":";

    {
        std::lock_guard<std::mutex> lock(g_drive_unc_mutex);
        auto it = g_drive_unc_cache.find(drive_key);
        if (it != g_drive_unc_cache.end())
        {
            if (!it->second.empty())
                return it->second + path.substr(2);
            return path;
        }
    }

    // Not cached, query WNetGetConnection
    wchar_t local_name[4] = {static_cast<wchar_t>(drive_letter), L':', L'\0', L'\0'};
    wchar_t remote_name[MAX_PATH * 4];
    DWORD remote_size = MAX_PATH * 4;

    DWORD result = WNetGetConnectionW(local_name, remote_name, &remote_size);

    std::string unc_prefix;
    if (result == NO_ERROR)
    {
        unc_prefix = wstring_to_utf8(std::wstring_view{remote_name});
    }

    {
        std::lock_guard<std::mutex> lock(g_drive_unc_mutex);
        g_drive_unc_cache[drive_key] = unc_prefix;
    }

    if (!unc_prefix.empty())
        return unc_prefix + path.substr(2);

    return path;
}

// --- normalize_path ---
std::string normalize_path(const std::string& path)
{
    if (path.empty())
        return {};

    std::string result = path;

    // Convert backslashes to forward slashes
    std::replace(result.begin(), result.end(), '\\', '/');

    // Collapse multiple slashes
    std::string collapsed;
    collapsed.reserve(result.size());
    bool last_was_slash = false;
    for (char c : result)
    {
        if (c == '/')
        {
            if (!last_was_slash)
                collapsed.push_back('/');
            last_was_slash = true;
        }
        else
        {
            collapsed.push_back(c);
            last_was_slash = false;
        }
    }

    // Remove trailing slash unless it's a UNC root or drive root
    if (collapsed.size() > 1 && collapsed.back() == '/')
    {
        bool is_unc_root = collapsed.size() >= 2 && collapsed[0] == '/' && collapsed[1] == '/';
        bool is_drive_root = collapsed.size() >= 3 && std::isalpha(collapsed[0]) && collapsed[1] == ':' && collapsed[2] == '/';
        if (!is_unc_root && !is_drive_root)
            collapsed.pop_back();
    }

// --- discover_installations ---
std::vector<DiscoveredInstall> discover_installations()
{
    std::vector<DiscoveredInstall> installs;

    // 1. Check registry: HKCU\Software\Transmission
    {
        auto config_dir = read_registry_string(HKEY_CURRENT_USER, L"Software\\Transmission", L"ConfigDir");
        if (config_dir)
        {
            DiscoveredInstall install;
            install.type = InstallType::Official;
            install.config_root = *config_dir;
            install.install_root = tr_sys_path_dirname(*config_dir);
            install.has_settings = dir_exists(*config_dir + "/settings.json");
            install.has_resume_dir = dir_exists(*config_dir + "/resume");
            install.has_torrents_dir = dir_exists(*config_dir + "/torrents");

            auto exe_path = read_registry_string(HKEY_CURRENT_USER, L"Software\\Transmission", L"ExePath");
            if (exe_path)
                install.exe_path = *exe_path;
            else
                install.exe_path = install.install_root + "/transmission-daemon.exe";

            install.version = get_exe_version(install.exe_path);
            if (install.version.empty())
                install.version = "unknown";

            installs.push_back(std::move(install));
        }
    }

    // 2. Check registry: HKLM\Software\Transmission
    {
        auto config_dir = read_registry_string(HKEY_LOCAL_MACHINE, L"Software\\Transmission", L"ConfigDir");
        if (config_dir)
        {
            DiscoveredInstall install;
            install.type = InstallType::Official;
            install.config_root = *config_dir;
            install.install_root = tr_sys_path_dirname(*config_dir);
            install.has_settings = dir_exists(*config_dir + "/settings.json");
            install.has_resume_dir = dir_exists(*config_dir + "/resume");
            install.has_torrents_dir = dir_exists(*config_dir + "/torrents");

            auto exe_path = read_registry_string(HKEY_LOCAL_MACHINE, L"Software\\Transmission", L"ExePath");
            if (exe_path)
                install.exe_path = *exe_path;
            else
                install.exe_path = install.install_root + "/transmission-daemon.exe";
// 3. Check standard paths
    std::vector<std::string> standard_paths = {
        expand_env_vars("%LOCALAPPDATA%\\transmission"),
        expand_env_vars("%APPDATA%\\Transmission"),
        "C:\\Program Files\\Transmission",
        "C:\\Program Files (x86)\\Transmission"
    };

    for (const auto& path : standard_paths)
    {
        if (dir_exists(path))
        {
            DiscoveredInstall install;
            install.type = InstallType::Official;
            install.install_root = path;
            install.config_root = path;
            install.has_settings = dir_exists(path + "/settings.json");
            install.has_resume_dir = dir_exists(path + "/resume");
            install.has_torrents_dir = dir_exists(path + "/torrents");
            install.exe_path = path + "/transmission-daemon.exe";
            install.version = get_exe_version(install.exe_path);
            if (install.version.empty())
                install.version = "unknown";
            installs.push_back(std::move(install));
        }
    }

    // 4. Check daemon config
    std::string daemon_config = expand_env_vars("%LOCALAPPDATA%\\transmission-daemon");
    if (dir_exists(daemon_config))
    {
        DiscoveredInstall install;
        install.type = InstallType::DaemonService;
        install.config_root = daemon_config;
        install.install_root = tr_sys_path_dirname(daemon_config);
        install.has_settings = dir_exists(daemon_config + "/settings.json");
        install.has_resume_dir = dir_exists(daemon_config + "/resume");
        install.has_torrents_dir = dir_exists(daemon_config + "/torrents");
        install.exe_path = install.install_root + "/transmission-daemon.exe";
        install.version = get_exe_version(install.exe_path);
        if (install.version.empty())
            install.version = "unknown";
        installs.push_back(std::move(install));
    }

    // 5. Check Microsoft Store
    std::string store_base = expand_env_vars("%LOCALAPPDATA%\\Packages");
    if (dir_exists(store_base))
    {
        tr_error error;
        if (auto dir = tr_sys_dir_open(store_base, &error); dir != TR_BAD_SYS_DIR)
        {
            for (;;)
            {
                char const* name = tr_sys_dir_read_name(dir, &error);
                if (!name)
                    break;

                if (std::string_view(name).find("Transmission") != std::string_view::npos)
                {
                    std::string package_path = tr_pathbuf{store_base, '/', name, "/LocalCache/Roaming/Transmission"}.str();
                    if (dir_exists(package_path))
                    {
                        DiscoveredInstall install;
                        install.type = InstallType::MicrosoftStore;
                        install.config_root = package_path;
                        install.install_root = tr_sys_path_dirname(package_path);
                        install.has_settings = dir_exists(package_path + "/settings.json");
                        install.has_resume_dir = dir_exists(package_path + "/resume");
                        install.has_torrents_dir = dir_exists(package_path + "/torrents");
                        install.exe_path = install.install_root + "/transmission-daemon.exe";
                        install.version = get_exe_version(install.exe_path);
                        if (install.version.empty())
                            install.version = "store";
                        installs.push_back(std::move(install));
                    }
                }
            }
            tr_sys_dir_close(dir, &error);
        }
    }

    // Sort by preference: Portable > Official > DaemonService > MS Store
    std::sort(installs.begin(), installs.end(),
        [](const DiscoveredInstall& a, const DiscoveredInstall& b)
        {
            auto rank = [](InstallType t) -> int
            {
                switch (t)
                {
                    case InstallType::Portable: return 0;
                    case InstallType::Official: return 1;
                    case InstallType::DaemonService: return 2;
                    case InstallType::MicrosoftStore: return 3;
                    default: return 4;
                }
// --- import_from_install ---
ImportResult import_from_install(const DiscoveredInstall& install, const PortableOptions& options)
{
    ImportResult result;

    if (install.type == InstallType::Unknown || install.config_root.empty())
    {
        result.errors.push_back("Invalid install: empty config root");
        return result;
    }

    tr_error error;

    // 1. Import settings.json
    if (options.import_settings && install.has_settings)
    {
        std::string settings_path = tr_pathbuf{install.config_root, "/settings.json"}.str();
        std::vector<char> contents;
        if (tr_file_read(settings_path, contents, &error))
        {
            tr_variant_serde serde = tr_variant_serde::json();
            auto parsed = serde.parse(std::string_view{contents.data(), contents.size()});
            if (parsed)
            {
                result.merged_settings = std::move(*parsed);
            }
            else
            {
                result.warnings.push_back(fmt::format("Failed to parse settings.json: {}", serde.error_.message()));
            }
        }
        else
        {
            result.warnings.push_back(fmt::format("Could not read settings.json: {}", error.message()));
        }
    }

    // 2. Import resume files
    if (options.import_resume && install.has_resume_dir)
    {
        std::string resume_dir = tr_pathbuf{install.config_root, "/resume"}.str();
        tr_error dir_error;
        if (auto dir = tr_sys_dir_open(resume_dir, &dir_error); dir != TR_BAD_SYS_DIR)
        {
            for (;;)
            {
                char const* name = tr_sys_dir_read_name(dir, &dir_error);
                if (!name)
                    break;

                std::string_view fname(name);
                if (!fname.ends_with(".resume"))
                    continue;

                std::string resume_path = tr_pathbuf{resume_dir, '/', fname}.str();
                std::vector<char> resume_data;
                if (!tr_file_read(resume_path, resume_data, &error))
                {
                    result.warnings.push_back(fmt::format("Could not read resume file '{}': {}", fname, error.message()));
                    result.skipped_torrents.emplace_back(fname);
                    continue;
                }

                tr_variant_serde serde = tr_variant_serde::benc();
                auto parsed = serde.parse(std::string_view{resume_data.data(), resume_data.size()});
                if (!parsed)
                {
                    result.warnings.push_back(fmt::format("Corrupt resume file '{}': {}", fname, serde.error_.message()));
                    result.skipped_torrents.emplace_back(fname);
                    continue;
                }

                // Extract destination path and remap if needed
                auto* map = parsed->get_if<tr_variant::Map>();
                if (map)
                {
                    if (auto* dest = map->find(tr_quark("destination")); dest != map->end())
                    {
                        if (auto* dest_str = dest->second.get_if<std::string>(); dest_str)
                        {
                            std::string remapped = options.remap_drive_to_unc ? drive_to_unc(*dest_str) : *dest_str;
                            remapped = normalize_path(remapped);
                            dest->second = tr_variant{remapped};
                        }
                    }
                }

                // Save to portable location
                std::string portable_resume_dir = tr_pathbuf{options.portable_root, "/resume"}.str();
                tr_error create_error;
                tr_sys_dir_create(portable_resume_dir, TR_SYS_DIR_CREATE_PARENTS, 0700, &create_error);

                std::string portable_resume_path = tr_pathbuf{portable_resume_dir, '/', fname}.str();
                std::string out_data = serde.to_string(*parsed);
                if (!tr_file_save(portable_resume_path, out_data, &error))
                {
                    result.warnings.push_back(fmt::format("Could not write portable resume '{}': {}", fname, error.message()));
                    result.skipped_torrents.emplace_back(fname);
                    continue;
                }

                result.imported_torrents.emplace_back(fname);
            }
            tr_sys_dir_close(dir, &dir_error);
        }
    }

    // 3. Import torrent files
    if (options.import_torrent_files && install.has_torrents_dir)
    {
        std::string torrents_dir = tr_pathbuf{install.config_root, "/torrents"}.str();
        tr_error dir_error;
        if (auto dir = tr_sys_dir_open(torrents_dir, &dir_error); dir != TR_BAD_SYS_DIR)
        {
            for (;;)
            {
                char const* name = tr_sys_dir_read_name(dir, &dir_error);
                if (!name)
                    break;

                std::string_view fname(name);
                if (!fname.ends_with(".torrent"))
                    continue;

                std::string src_path = tr_pathbuf{torrents_dir, '/', fname}.str();
                std::string dst_path = tr_pathbuf{options.portable_root, "/torrents", fname}.str();

                tr_error create_error;
                tr_sys_dir_create(tr_sys_path_dirname(dst_path), TR_SYS_DIR_CREATE_PARENTS, 0700, &create_error);

                if (!tr_file_move(src_path, dst_path, true, &error))
                {
                    result.warnings.push_back(fmt::format("Could not copy torrent file '{}': {}", fname, error.message()));
                    result.skipped_torrents.emplace_back(fname);
                    continue;
                }

                result.imported_torrents.emplace_back(fname);
            }
            tr_sys_dir_close(dir, &dir_error);
        }
    }

    result.success = result.errors.empty();
    return result;
}

} // namespace tr
            };
            return rank(a.type) < rank(b.type);
        });

    return installs;
}

            install.version = get_exe_version(install.exe_path);
            if (install.version.empty())
                install.version = "unknown";

            installs.push_back(std::move(install));
        }
    }
    return collapsed;
}

// --- is_nas_path ---
bool is_nas_path(const std::string& path)
{
    if (path.size() >= 2 && path[0] == '/' && path[1] == '/')
        return true; // UNC path

    // Check if drive letter maps to network drive
    if (path.size() >= 2 && path[1] == ':')
    {
        char drive_letter = std::toupper(static_cast<unsigned char>(path[0]));
        if (drive_letter >= 'A' && drive_letter <= 'Z')
        {
            wchar_t local_name[4] = {static_cast<wchar_t>(drive_letter), L':', L'\0', L'\0'};
            wchar_t remote_name[MAX_PATH * 4];
            DWORD remote_size = MAX_PATH * 4;
            DWORD result = WNetGetConnectionW(local_name, remote_name, &remote_size);
            return result == NO_ERROR;
        }
    }

    return false;
}

// --- has_portable_marker ---
bool has_portable_marker(const std::string& exe_path)
{
    std::string marker = tr_sys_path_dirname(exe_path);
    marker = tr_pathbuf{marker, "/portable.dat"}.str();
    return dir_exists(marker);
}

// --- get_portable_root ---
std::string get_portable_root(const std::string& exe_path)
{
    return tr_sys_path_dirname(exe_path);
}

// --- is_portable_mode ---
bool is_portable_mode(int argc, char* argv[], const std::string& exe_path)
{
    for (int i = 1; i < argc; ++i)
    {
        if (std::string_view(argv[i]) == "--portable"sv)
            return true;
    }
    return has_portable_marker(exe_path);
}