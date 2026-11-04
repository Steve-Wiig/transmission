#include "libtransmission/portable-discovery.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <lm.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <winreg.h>

#pragma comment(lib, "netapi32.lib")
#pragma comment(lib, "shlwapi.lib")

#include <fmt/format.h>

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

std::wstring utf8_to_wide(std::string_view str)
{
    if (str.empty())
        return {};

    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
    if (size_needed <= 0)
        return {};

    std::wstring result(size_needed, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), size_needed);
    return result;
}

std::string get_exe_path()
{
    std::wstring path(MAX_PATH, L'\0');
    DWORD len = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    while (len == path.size())
    {
        path.resize(path.size() * 2);
        len = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    }
    path.resize(len);
    return wide_to_utf8(path);
}

std::string get_exe_dir()
{
    return dir_name(get_exe_path());
}

bool file_exists(std::string_view path)
{
    std::wstring wpath = utf8_to_wide(path);
    DWORD attrs = GetFileAttributesW(wpath.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

bool dir_exists(std::string_view path)
{
    std::wstring wpath = utf8_to_wide(path);
    DWORD attrs = GetFileAttributesW(wpath.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

std::string read_registry_string(HKEY root, std::wstring_view subkey, std::wstring_view value_name)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subkey.data(), 0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS)
        return {};

    DWORD type = 0;
    DWORD size = 0;
    if (RegQueryValueExW(key, value_name.data(), nullptr, &type, nullptr, &size) != ERROR_SUCCESS || type != REG_SZ || size == 0)
    {
        RegCloseKey(key);
        return {};
    }

    std::wstring result(size / sizeof(wchar_t), L'\0');
    if (RegQueryValueExW(key, value_name.data(), nullptr, &type, reinterpret_cast<LPBYTE>(result.data()), &size) != ERROR_SUCCESS)
    {
        RegCloseKey(key);
        return {};
    }

    RegCloseKey(key);
    if (!result.empty() && result.back() == L'\0')
        result.pop_back();

    return wide_to_utf8(result);
}

std::string get_official_install_root()
{
    // Check HKLM first (system-wide install), then HKCU (per-user)
    std::string root = read_registry_string(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Transmission", L"InstallDir");
    if (root.empty())
        root = read_registry_string(HKEY_CURRENT_USER, L"SOFTWARE\\Transmission", L"InstallDir");
    return root;
}

std::string get_official_config_root()
{
    // Official installs use %APPDATA%\transmission
    PWSTR appdata = nullptr;
    if (SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appdata) == S_OK)
    {
        std::string result = wide_to_utf8(appdata);
        CoTaskMemFree(appdata);
        result += "\\transmission";
        return result;
    }
    return {};
}

std::string get_official_version()
{
    std::string version = read_registry_string(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Transmission", L"Version");
    if (version.empty())
        version = read_registry_string(HKEY_CURRENT_USER, L"SOFTWARE\\Transmission", L"Version");
    return version;
}

std::string get_microsoft_store_root()
{
    // Microsoft Store apps are in WindowsApps folder, hard to access directly
    // Try to find via package family name
    std::string local_appdata;
    PWSTR path = nullptr;
    if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path) == S_OK)
    {
        local_appdata = wide_to_utf8(path);
        CoTaskMemFree(path);
    }

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
                if (name.find("Transmission") != std::string::npos || name.find("transmission") != std::string::npos)
                {
                    std::string local_state = entry.path().string() + "\\LocalState";
                    if (dir_exists(local_state))
                        return local_state;
                }
            }
        }
    }
    return {};
}

std::string get_microsoft_store_config_root()
{
    // Microsoft Store apps use LocalState for config
    std::string root = get_microsoft_store_root();
    if (!root.empty())
        return root + "\\config";
    return {};
}

std::vector<DiscoveredInstall> discover_installations()
{
    std::vector<DiscoveredInstall> installs;

    // 1. Official installation (MSI/Installer)
    {
        std::string install_root = get_official_install_root();
        if (!install_root.empty() && dir_exists(install_root))
        {
            DiscoveredInstall install;
            install.type = InstallType::Official;
            install.install_root = normalize_path(install_root);
            install.config_root = normalize_path(get_official_config_root());
            install.version = get_official_version();
            install.exe_path = normalize_path(install_root + "\\transmission-qt.exe");

            // Check what data exists
            install.has_settings = file_exists(install.config_root + "\\settings.json");
            install.has_resume_dir = dir_exists(install.config_root + "\\resume");
            install.has_torrents_dir = dir_exists(install.config_root + "\\torrents");

            if (install.version.empty())
                install.warnings.emplace_back("Could not determine version from registry");

            installs.push_back(std::move(install));
        }
    }

    // 2. Microsoft Store installation
    {
        std::string install_root = get_microsoft_store_root();
        if (!install_root.empty() && dir_exists(install_root))
        {
            DiscoveredInstall install;
            install.type = InstallType::MicrosoftStore;
            install.install_root = normalize_path(install_root);
            install.config_root = normalize_path(get_microsoft_store_config_root());
            install.exe_path = normalize_path(install_root + "\\transmission-qt.exe");

            install.has_settings = file_exists(install.config_root + "\\settings.json");
            install.has_resume_dir = dir_exists(install.config_root + "\\resume");
            install.has_torrents_dir = dir_exists(install.config_root + "\\torrents");

            installs.push_back(std::move(install));
        }
    }

    // 3. Portable installation (check current exe directory for portable.dat)
    {
        std::string exe_path = get_exe_path();
        std::string portable_root = get_portable_root(exe_path);
        std::string marker_path = portable_root + "\\portable.dat";

        if (file_exists(marker_path))
        {
            DiscoveredInstall install;
            install.type = InstallType::Portable;
            install.install_root = normalize_path(portable_root);
            install.config_root = normalize_path(portable_root + "\\config");
            install.exe_path = normalize_path(exe_path);

            // Try to read version from a version file or executable
            std::string version_file = portable_root + "\\version.txt";
            if (file_exists(version_file))
            {
                std::ifstream f(version_file);
                std::getline(f, install.version);
            }
            if (install.version.empty())
            {
                // Try to get from exe version resource
                DWORD handle = 0;
                std::wstring wexe = utf8_to_wide(exe_path);
                DWORD size = GetFileVersionInfoSizeW(wexe.c_str(), &handle);
                if (size > 0)
                {
                    std::vector<BYTE> buf(size);
                    if (GetFileVersionInfoW(wexe.c_str(), 0, size, buf.data()))
                    {
                        VS_FIXEDFILEINFO* info = nullptr;
                        UINT len = 0;
                        if (VerQueryValueW(buf.data(), L"\\", reinterpret_cast<LPVOID*>(&info), &len) && info)
                        {
                            install.version = fmt::format("{}.{}.{}.{}",
                                HIWORD(info->dwFileVersionMS),
                                LOWORD(info->dwFileVersionMS),
                                HIWORD(info->dwFileVersionLS),
                                LOWORD(info->dwFileVersionLS));
                        }
                    }
                }
            }

            install.has_settings = file_exists(install.config_root + "\\settings.json");
            install.has_resume_dir = dir_exists(install.config_root + "\\resume");
            install.has_torrents_dir = dir_exists(install.config_root + "\\torrents");

            installs.push_back(std::move(install));
        }
    }

    return installs;
}

bool is_portable_mode(int argc, char* argv[], std::string const& exe_path)
{
    // Check command line for --portable
    for (int i = 1; i < argc; ++i)
    {
        if (argv[i] != nullptr && std::string_view{argv[i]} == "--portable")
        {
            return true;
        }
    }

    // Check for portable.dat marker next to executable
    std::string portable_root = get_portable_root(exe_path);
    std::string marker_path = portable_root + "\\portable.dat";
    return file_exists(marker_path);
}

std::string get_portable_root(std::string const& exe_path)
{
    return dir_name(exe_path);
}

bool has_portable_marker(std::string const& exe_path)
{
    std::string portable_root = get_portable_root(exe_path);
    std::string marker_path = portable_root + "\\portable.dat";
    return file_exists(marker_path);
}

std::string drive_to_unc(std::string const& path)
{
    if (path.size() < 2 || path[1] != ':')
        return path; // Not a drive letter path

    char drive_letter = std::toupper(path[0]);
    std::wstring drive = L" :";
    drive[0] = drive_letter;

    wchar_t unc_path[MAX_PATH];
    DWORD unc_size = MAX_PATH;

    DWORD result = WNetGetUniversalNameW(drive.c_str(), UNIVERSAL_NAME_INFO_LEVEL, unc_path, &unc_size);
    if (result == NO_ERROR)
    {
        std::wstring unc(unc_path);
        // Append the rest of the path after the drive letter
        std::string rest = path.substr(2);
        std::replace(rest.begin(), rest.end(), '\\', '/');
        return wide_to_utf8(unc) + rest;
    }

    return path; // Could not map, return original
}

std::string normalize_path(std::string const& path)
{
    std::string result = path;
    std::replace(result.begin(), result.end(), '\\', '/');

    // Collapse multiple slashes
    std::string::size_type pos = 0;
    while ((pos = result.find("//", pos)) != std::string::npos)
    {
        if (pos > 0 && result[pos - 1] != ':') // Don't collapse after "C:"
        {
            result.erase(pos, 1);
        }
        else
        {
            ++pos;
        }
    }

    // Remove trailing slash (unless it's a root like "C:/")
    if (result.size() > 3 && result.back() == '/')
    {
        result.pop_back();
    }

    return result;
}

bool is_nas_path(std::string const& path)
{
    // UNC paths start with \\ or //
    return path.size() >= 2 && path[0] == '\\' && path[1] == '\\';
}

// Helper to copy directory recursively
bool copy_directory(std::string const& src, std::string const& dst, std::vector<std::string>& copied, std::vector<std::string>& errors)
{
    std::wstring wsrc = utf8_to_wide(src);
    std::wstring wdst = utf8_to_wide(dst);

    if (!CreateDirectoryW(wdst.c_str(), nullptr))
    {
        DWORD err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS)
        {
            errors.emplace_back("Failed to create directory: " + dst + " (error " + std::to_string(err) + ")");
            return false;
        }
    }

    bool success = true;
    for (auto const& entry : std::filesystem::directory_iterator(src))
    {
        std::string src_file = normalize_path(entry.path().string());
        std::string dst_file = normalize_path(dst + "/" + entry.path().filename().string());

        if (entry.is_directory())
        {
            if (!copy_directory(src_file, dst_file, copied, errors))
                success = false;
        }
        else
        {
            std::wstring wsrc_file = utf8_to_wide(src_file);
            std::wstring wdst_file = utf8_to_wide(dst_file);
            if (!CopyFileW(wsrc_file.c_str(), wdst_file.c_str(), FALSE))
            {
                DWORD err = GetLastError();
                if (err != ERROR_FILE_EXISTS)
                {
                    errors.emplace_back("Failed to copy " + src_file + " to " + dst_file + " (error " + std::to_string(err) + ")");
                    success = false;
                }
            }
            else
            {
                copied.push_back(dst_file);
            }
        }
    }
    return success;
}

// Helper to merge settings.json files
std::optional<tr_variant> merge_settings(std::string const& src_settings, std::string const& dst_settings, std::vector<std::string>& warnings)
{
    // For now, just return the source settings as-is
    // A full implementation would parse both and merge intelligently
    if (!file_exists(src_settings))
        return std::nullopt;

    try
    {
        std::ifstream f(src_settings);
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        tr_variant v = tr_variant_parse_json(content);
        warnings.emplace_back("Settings imported from " + src_settings + " (manual merge may be needed)");
        return v;
    }
    catch (std::exception const& e)
    {
        warnings.emplace_back("Failed to parse settings: " + std::string(e.what()));
        return std::nullopt;
    }
}

ImportResult import_from_install(DiscoveredInstall const& install, PortableOptions const& options)
{
    ImportResult result;
    result.success = false;

    if (install.type == InstallType::Unknown)
    {
        result.errors.emplace_back("Unknown installation type");
        return result;
    }

    if (install.config_root.empty())
    {
        result.errors.emplace_back("Source config root is empty");
        return result;
    }

    std::string portable_config = options.portable_root + "\\config";

    // Import settings.json
    if (options.import_settings && install.has_settings)
    {
        std::string src_settings = install.config_root + "\\settings.json";
        std::string dst_settings = portable_config + "\\settings.json";

        if (!file_exists(dst_settings) || options.fail_fast)
        {
            auto merged = merge_settings(src_settings, dst_settings, result.warnings);
            if (merged)
            {
                result.merged_settings = std::move(*merged);
                result.imported_torrents.push_back("settings.json"); // Reusing field for tracking
            }
            else if (options.fail_fast)
            {
                result.errors.emplace_back("Failed to import settings");
                return result;
            }
        }
        else
        {
            result.skipped_torrents.push_back("settings.json (already exists)");
        }
    }

    // Import resume data
    if (options.import_resume && install.has_resume_dir)
    {
        std::string src_resume = install.config_root + "\\resume";
        std::string dst_resume = portable_config + "\\resume";

        if (!dir_exists(dst_resume) || options.fail_fast)
        {
            std::vector<std::string> copied;
            std::vector<std::string> errors;
            if (copy_directory(src_resume, dst_resume, copied, errors))
            {
                result.imported_torrents.insert(result.imported_torrents.end(), copied.begin(), copied.end());
            }
            result.errors.insert(result.errors.end(), errors.begin(), errors.end());
            if (!errors.empty() && options.fail_fast)
                return result;
        }
        else
        {
            result.skipped_torrents.push_back("resume/ (already exists)");
        }
    }

    // Import torrent files
    if (options.import_torrent_files && install.has_torrents_dir)
    {
        std::string src_torrents = install.config_root + "\\torrents";
        std::string dst_torrents = portable_config + "\\torrents";

        if (!dir_exists(dst_torrents) || options.fail_fast)
        {
            std::vector<std::string> copied;
            std::vector<std::string> errors;
            if (copy_directory(src_torrents, dst_torrents, copied, errors))
            {
                result.imported_torrents.insert(result.imported_torrents.end(), copied.begin(), copied.end());
            }
            result.errors.insert(result.errors.end(), errors.begin(), errors.end());
            if (!errors.empty() && options.fail_fast)
                return result;
        }
        else
        {
            result.skipped_torrents.push_back("torrents/ (already exists)");
        }
    }

    result.success = result.errors.empty();
    return result;
}

} // namespace tr
