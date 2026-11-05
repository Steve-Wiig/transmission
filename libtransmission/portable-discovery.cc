// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "libtransmission/portable-discovery.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h> // SHGetKnownFolderPath
#include <knownfolders.h> // FOLDERID_*
#include <strsafe.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#endif

#include <libtransmission/file.h>
#include <libtransmission/file-utils.h>

namespace tr
{
namespace
{

std::string dir_name(std::string_view path)
{
    if (path.empty())
    {
        return {};
    }

    auto const pos = path.find_last_of("/\\");

    if (pos == std::string_view::npos)
    {
        return ".";
    }

    if (pos == 0)
    {
        return "/";
    }

    return std::string{path.substr(0, pos)};
}

// Helper to join path components
std::string join_path(std::string_view a, std::string_view b)
{
    if (a.empty())
    {
        return std::string{b};
    }
    if (b.empty())
    {
        return std::string{a};
    }
    bool a_ends_with_sep = (a.back() == '/' || a.back() == '\\');
    bool b_starts_with_sep = (b.front() == '/' || b.front() == '\\');
    if (a_ends_with_sep && b_starts_with_sep)
    {
        return std::string{a.substr(0, a.size() - 1)} + std::string{b};
    }
    if (!a_ends_with_sep && !b_starts_with_sep)
    {
        return std::string{a} + "/" + std::string{b};
    }
    return std::string{a} + std::string{b};
}

#ifdef _WIN32

// Helper to convert wide string to UTF-8
std::string wide_to_utf8(std::wstring_view wstr)
{
    if (wstr.empty())
    {
        return {};
    }
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0)
    {
        return {};
    }
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), result.data(), size, nullptr, nullptr);
    return result;
}

// Helper to check if a path exists and is a directory
bool path_exists(std::string const& path)
{
    return tr_sys_path_exists(path);
}

// Helper to read registry value
std::optional<std::string> read_registry_string(HKEY root, std::wstring_view subkey, std::wstring_view value_name)
{
    HKEY key = nullptr;
    LSTATUS status = RegOpenKeyExW(root, subkey.data(), 0, KEY_READ | KEY_WOW64_64KEY, &key);
    if (status != ERROR_SUCCESS)
    {
        return std::nullopt;
    }

    // First, get the size
    DWORD data_size = 0;
    status = RegQueryValueExW(key, value_name.data(), nullptr, nullptr, nullptr, &data_size);
    if (status != ERROR_SUCCESS || data_size == 0)
    {
        RegCloseKey(key);
        return std::nullopt;
    }

    // Read the value
    std::vector<BYTE> data(data_size);
    status = RegQueryValueExW(key, value_name.data(), nullptr, nullptr, data.data(), &data_size);
    RegCloseKey(key);

    if (status != ERROR_SUCCESS)
    {
        return std::nullopt;
    }

    // Convert from wide string (REG_SZ) to UTF-8
    std::wstring_view wstr(reinterpret_cast<wchar_t const*>(data.data()), data_size / sizeof(wchar_t));
    // Remove trailing null if present
    if (!wstr.empty() && wstr.back() == L'\0')
    {
        wstr.remove_suffix(1);
    }

    return wide_to_utf8(wstr);
}

// Helper to get known folder path
std::optional<std::string> get_known_folder_path(REFKNOWNFOLDERID rfid)
{
    PWSTR path = nullptr;
    HRESULT hr = SHGetKnownFolderPath(rfid, 0, nullptr, &path);
    if (hr != S_OK || path == nullptr)
    {
        return std::nullopt;
    }

    std::string result = wide_to_utf8(path);
    CoTaskMemFree(path);
    return result;
}

// Build a DiscoveredInstall from an install root path
std::optional<DiscoveredInstall> make_discovered_install(std::string install_root, InstallType type)
{
    install_root = normalize_path(install_root);

    if (!path_exists(install_root))
    {
        return std::nullopt;
    }

    DiscoveredInstall install;
    install.type = type;
    install.install_root = install_root;

    // Config root is typically the same as install root for portable, or a subdirectory
    // For official installs, config is usually in APPDATA/LOCALAPPDATA
    if (type == InstallType::Portable)
    {
        install.config_root = install_root;
    }
    else
    {
        // For official installs, check common config locations
        std::string config_root = install_root;
        install.config_root = config_root;
    }

    // Check for settings.json
    install.has_settings = path_exists(join_path(install.config_root, "settings.json"));

    // Check for resume directory
    install.has_resume_dir = path_exists(join_path(install.config_root, "resume"));

    // Check for torrents directory
    install.has_torrents_dir = path_exists(join_path(install.config_root, "torrents"));

    // Try to find version
    // Check for version file or executable
    std::string exe_path = join_path(install_root, "transmission-daemon.exe");
    if (!path_exists(exe_path))
    {
        exe_path = join_path(install_root, "transmission-qt.exe");
    }
    if (!path_exists(exe_path))
    {
        exe_path = join_path(install_root, "transmission-gtk.exe");
    }
    if (path_exists(exe_path))
    {
        install.exe_path = exe_path;
        // Could extract version from exe here if needed
    }

    return install;
}

#endif // _WIN32

} // namespace

std::vector<DiscoveredInstall> discover_installations()
{
    std::vector<DiscoveredInstall> results;

#ifdef _WIN32
    // 1. Registry - HKCU (higher priority than HKLM for user installs)
    {
        auto install_root = read_registry_string(HKEY_CURRENT_USER, L"Software\\Transmission", L"InstallDir");
        if (install_root)
        {
            if (auto install = make_discovered_install(*install_root, InstallType::Official))
            {
                results.push_back(*install);
            }
        }
    }

    {
        auto install_root = read_registry_string(HKEY_LOCAL_MACHINE, L"Software\\Transmission", L"InstallDir");
        if (install_root)
        {
            if (auto install = make_discovered_install(*install_root, InstallType::Official))
            {
                results.push_back(*install);
            }
        }
    }

    // 2. Known Folders
    // %LOCALAPPDATA%\Transmission
    {
        auto path = get_known_folder_path(FOLDERID_LocalAppData);
        if (path)
        {
            std::string install_root = join_path(*path, "Transmission");
            if (auto install = make_discovered_install(install_root, InstallType::Official))
            {
                results.push_back(*install);
            }
        }
    }

    // %APPDATA%\Transmission (Roaming)
    {
        auto path = get_known_folder_path(FOLDERID_RoamingAppData);
        if (path)
        {
            std::string install_root = join_path(*path, "Transmission");
            if (auto install = make_discovered_install(install_root, InstallType::Official))
            {
                results.push_back(*install);
            }
        }
    }

    // %PROGRAMDATA%\Transmission (All users)
    {
        auto path = get_known_folder_path(FOLDERID_ProgramData);
        if (path)
        {
            std::string install_root = join_path(*path, "Transmission");
            if (auto install = make_discovered_install(install_root, InstallType::Official))
            {
                results.push_back(*install);
            }
        }
    }

    // 3. Program Files
    // C:\Program Files\Transmission
    {
        auto path = get_known_folder_path(FOLDERID_ProgramFiles);
        if (path)
        {
            std::string install_root = join_path(*path, "Transmission");
            if (auto install = make_discovered_install(install_root, InstallType::Official))
            {
                results.push_back(*install);
            }
        }
    }

    // C:\Program Files (x86)\Transmission
    {
        auto path = get_known_folder_path(FOLDERID_ProgramFilesX86);
        if (path)
        {
            std::string install_root = join_path(*path, "Transmission");
            if (auto install = make_discovered_install(install_root, InstallType::Official))
            {
                results.push_back(*install);
            }
        }
    }

    // Sort by priority: Official > Portable > MicrosoftStore > DaemonService > Unknown
    // Within same type, prefer ones with settings/resume/torrents
    std::sort(results.begin(), results.end(),
        [](DiscoveredInstall const& a, DiscoveredInstall const& b)
        {
            if (static_cast<int>(a.type) != static_cast<int>(b.type))
            {
                return static_cast<int>(a.type) < static_cast<int>(b.type);
            }
            // Prefer installs with more complete data
            int a_score = (a.has_settings ? 1 : 0) + (a.has_resume_dir ? 1 : 0) + (a.has_torrents_dir ? 1 : 0);
            int b_score = (b.has_settings ? 1 : 0) + (b.has_resume_dir ? 1 : 0) + (b.has_torrents_dir ? 1 : 0);
            return a_score > b_score;
        });
#endif // _WIN32

    return results;
}

bool is_portable_mode(int argc, char* argv[], std::string const& /*exe_path*/)
{
    for (int i = 1; i < argc; ++i)
    {
        if (argv[i] != nullptr && std::string_view{argv[i]} == "--portable")
        {
            return true;
        }
    }

    return false;
}

std::string get_portable_root(std::string const& exe_path)
{
    return dir_name(exe_path);
}

bool has_portable_marker(std::string const& /*exe_path*/)
{
    return false;
}

ImportResult import_from_install(
    DiscoveredInstall const& /*install*/,
    PortableOptions const& /*options*/)
{
    ImportResult result;
    result.success = true;
    result.warnings.emplace_back("portable-discovery is currently stubbed for daemon build");
    return result;
}

std::string drive_to_unc(std::string const& path)
{
    return path;
}

std::string normalize_path(std::string const& path)
{
    std::string result = path;
    std::replace(result.begin(), result.end(), '\\', '/');
    return result;
}

bool is_nas_path(std::string const& path)
{
    return path.size() >= 2 && path[0] == '/' && path[1] == '/';
}

} // namespace tr
