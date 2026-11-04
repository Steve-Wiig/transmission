// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "PortableDiscovery.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <winreg.h>
#include <filesystem>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>

namespace tr
{
namespace portable
{
namespace
{

std::string wide_to_utf8(std::wstring_view wstr)
{
    if (wstr.empty())
        return {};

    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    if (size_needed <= 0)
        return {};

    std::string result(size_needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), result.data(), size_needed, nullptr, nullptr);
    return result;
}

bool dir_exists(std::string_view path)
{
    std::wstring wpath;
    wpath.assign(path.begin(), path.end());
    DWORD attrs = GetFileAttributesW(wpath.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

std::string get_known_folder_path(REFKNOWNFOLDERID rfid)
{
    PWSTR path = nullptr;
    if (SHGetKnownFolderPath(rfid, 0, nullptr, &path) == S_OK)
    {
        std::string result = wide_to_utf8(path);
        CoTaskMemFree(path);
        return result;
    }
    return {};
}

std::string read_registry_string(HKEY root, std::wstring_view subkey, std::wstring_view value_name, REGSAM sam_flags = KEY_READ | KEY_WOW64_64KEY)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subkey.data(), 0, sam_flags, &key) != ERROR_SUCCESS)
        return {};

    std::unique_ptr<std::remove_pointer_t<HKEY>, decltype(&RegCloseKey)> key_guard(key, RegCloseKey);

    DWORD type = 0;
    DWORD size = 0;
    if (RegQueryValueExW(key, value_name.data(), nullptr, &type, nullptr, &size) != ERROR_SUCCESS || type != REG_SZ || size == 0)
        return {};

    std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1);
    if (RegQueryValueExW(key, value_name.data(), nullptr, &type, reinterpret_cast<LPBYTE>(buffer.data()), &size) != ERROR_SUCCESS)
        return {};

    return wide_to_utf8(buffer.data());
}

std::string get_registry_install_path(HKEY root, std::wstring_view subkey)
{
    // Try multiple common value names
    static constexpr std::wstring_view value_names[] = {
        L"InstallLocation",
        L"InstallPath",
        L"Path",
        L"InstallDir"
    };

    for (auto const& val : value_names)
    {
        std::string path = read_registry_string(root, subkey, val);
        if (!path.empty() && dir_exists(path))
            return path;
    }

    // Fallback: try default value (empty string)
    std::string path = read_registry_string(root, subkey, L"");
    if (!path.empty() && dir_exists(path))
        return path;

    return {};
}

void scan_uninstall_key(HKEY root, std::wstring_view base_key, std::vector<HostInstall>& installs, std::string const& type)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, base_key.data(), 0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS)
        return;

    std::unique_ptr<std::remove_pointer_t<HKEY>, decltype(&RegCloseKey)> key_guard(key, RegCloseKey);

    DWORD index = 0;
    wchar_t subkey_name[256];
    while (true)
    {
        DWORD name_size = std::size(subkey_name);
        LSTATUS status = RegEnumKeyExW(key, index, subkey_name, &name_size, nullptr, nullptr, nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS)
            break;
        if (status != ERROR_SUCCESS)
        {
            ++index;
            continue;
        }

        // Check if this subkey is Transmission-related
        std::wstring subkey_wstr(subkey_name);
        std::transform(subkey_wstr.begin(), subkey_wstr.end(), subkey_wstr.begin(), ::towlower);
        if (subkey_wstr.find(L"transmission") == std::wstring_view::npos)
        {
            ++index;
            continue;
        }

        std::wstring full_subkey = std::wstring(base_key) + L"\\" + subkey_wstr;
        std::string install_dir = get_registry_install_path(root, full_subkey);

        if (!install_dir.empty())
        {
            // Look for config directory
            std::string config_dir;
            std::string local_appdata = get_known_folder_path(FOLDERID_LocalAppData);
            if (!local_appdata.empty())
            {
                config_dir = local_appdata + "\\transmission";
                if (!dir_exists(config_dir))
                    config_dir.clear();
            }

            HostInstall install;
            install.type = type;
            install.install_dir = install_dir;
            install.config_dir = config_dir;
            installs.push_back(std::move(install));
        }

        ++index;
    }
}

} // anonymous namespace

std::vector<HostInstall> discoverHostInstalls()
{
    std::vector<HostInstall> installs;

    // 1. Scan HKLM\Software\Microsoft\Windows\CurrentVersion\Uninstall (64-bit)
    scan_uninstall_key(
        HKEY_LOCAL_MACHINE,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        installs,
        "official");

    // 2. Scan HKLM\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall (32-bit on 64-bit)
    scan_uninstall_key(
        HKEY_LOCAL_MACHINE,
        L"Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        installs,
        "official");

    // 3. Scan HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall
    scan_uninstall_key(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        installs,
        "official");

    // 4. Known Folder fallback: %LOCALAPPDATA%\transmission
    std::string local_appdata = get_known_folder_path(FOLDERID_LocalAppData);
    if (!local_appdata.empty())
    {
        std::string config_dir = local_appdata + "\\transmission";
        if (dir_exists(config_dir))
        {
            // Check if we already have this config_dir
            bool found = false;
            for (auto const& inst : installs)
            {
                if (inst.config_dir == config_dir)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                HostInstall install;
                install.type = "official";
                install.config_dir = config_dir;
                installs.push_back(std::move(install));
            }
        }
    }

    // 5. Microsoft Store installation
    if (!local_appdata.empty())
    {
        std::string packages_root = local_appdata + "\\Packages";
        if (dir_exists(packages_root))
        {
            for (auto const& entry : std::filesystem::directory_iterator(packages_root))
            {
                if (!entry.is_directory())
                    continue;

                std::string name = entry.path().filename().string();
                std::string name_lower = name;
                std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
                if (name_lower.find("transmission") == std::string::npos)
                    continue;

                std::string local_state = entry.path().string() + "\\LocalState";
                if (dir_exists(local_state))
                {
                    std::string config_dir = local_state + "\\config";
                    bool found = false;
                    for (auto const& inst : installs)
                    {
                        if (inst.config_dir == config_dir)
                        {
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                    {
                        HostInstall install;
                        install.type = "msstore";
                        install.config_dir = config_dir;
                        installs.push_back(std::move(install));
                    }
                    break; // Only need one MS Store install
                }
            }
        }
    }

    return installs;
}

} // namespace portable
} // namespace tr

#else // !_WIN32

namespace tr
{
namespace portable
{

std::vector<HostInstall> discoverHostInstalls()
{
    return {};
}

} // namespace portable
} // namespace tr

#endif // _WIN32
