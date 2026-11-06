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
bool dir_exists(std::string const& path)
{
    return tr_sys_path_exists(path);
}

bool file_exists(std::string const& path)
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

    // Config root detection varies by install type
    if (type == InstallType::Portable)
    {
        install.config_root = install_root;
    }
    else if (type == InstallType::Official)
    {
        // Official installs store config in LocalAppData, not Program Files
        wchar_t* local_appdata = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local_appdata)))
        {
            install.config_root = wide_to_utf8(local_appdata) + "\\transmission";
            CoTaskMemFree(local_appdata);
        }
        else
        {
            install.config_root = install_root; // Fallback
        }
    }
    else if (type == InstallType::MicrosoftStore)
    {
        // MS Store apps use LocalState\config
        install.config_root = install_root + "\\LocalState\\config";
    }
    else
    {
        install.config_root = install_root;
    }

    // Validate the config directory actually has Transmission data
    if (!dir_exists(install.config_root))
    {
        return std::nullopt;
    }

    // Check for settings.json
    install.has_settings = path_exists(join_path(install.config_root, "settings.json"));

    // Check for resume directory with actual .resume files
    std::string resume_dir = join_path(install.config_root, "resume");
    install.has_resume_dir = false;
    if (dir_exists(resume_dir)) {
        for (auto const& entry : std::filesystem::directory_iterator(resume_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".resume") {
                install.has_resume_dir = true;
                break;
            }
        }
    }

    // Check for torrents directory with actual .torrent/.magnet files
    std::string torrents_dir = join_path(install.config_root, "torrents");
    install.has_torrents_dir = false;
    if (dir_exists(torrents_dir)) {
        for (auto const& entry : std::filesystem::directory_iterator(torrents_dir)) {
            if (entry.is_regular_file() && 
                (entry.path().extension() == ".torrent" || entry.path().extension() == ".magnet")) {
                install.has_torrents_dir = true;
                break;
            }
        }
    }

    // Try to find version
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
    }

    return install;
}

#endif // _WIN32

} // namespace

std::vector<DiscoveredInstall> discover_installations()
{
    std::vector<DiscoveredInstall> results;

#ifdef _WIN32
    // Helper to validate Transmission config directory - checks for non-empty dirs
    auto is_valid_transmission_config = [](std::string_view path) -> bool {
        std::string resume_dir = std::string(path) + "\\resume";
        std::string torrents_dir = std::string(path) + "\\torrents";
        std::string settings_file = std::string(path) + "\\settings.json";
        
        // Must have settings.json AND at least one non-empty data directory
        if (!file_exists(settings_file))
            return false;
            
        if (dir_exists(resume_dir)) {
            // Check if resume dir has any .resume files
            for (auto const& entry : std::filesystem::directory_iterator(resume_dir)) {
                if (entry.is_regular_file() && entry.path().extension() == ".resume") {
                    return true;
                }
            }
        }
        
        if (dir_exists(torrents_dir)) {
            // Check if torrents dir has any .torrent or .magnet files
            for (auto const& entry : std::filesystem::directory_iterator(torrents_dir)) {
                if (entry.is_regular_file() && 
                    (entry.path().extension() == ".torrent" || entry.path().extension() == ".magnet")) {
                    return true;
                }
            }
        }
        
        return false;
    };

    auto add_install_if_valid = [&results, &is_valid_transmission_config](std::string const& config_dir, std::string const& type) {
        if (dir_exists(config_dir) && is_valid_transmission_config(config_dir)) {
            // Check if we already have this config_dir
            bool found = false;
            for (auto const& inst : results) {
                if (inst.config_dir == config_dir) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                DiscoveredInstall install;
                install.type = type == "official" ? InstallType::Official :
                               type == "msstore" ? InstallType::MicrosoftStore :
                               InstallType::Unknown;
                install.config_dir = config_dir;
                results.push_back(std::move(install));
            }
        }
    };

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
            std::string config_dir = join_path(*path, "Transmission");
            add_install_if_valid(config_dir, "official");
        }
    }

    // %APPDATA%\Transmission (Roaming)
    {
        auto path = get_known_folder_path(FOLDERID_RoamingAppData);
        if (path)
        {
            std::string config_dir = join_path(*path, "Transmission");
            add_install_if_valid(config_dir, "official");
        }
    }

    // %PROGRAMDATA%\Transmission (All users)
    {
        auto path = get_known_folder_path(FOLDERID_ProgramData);
        if (path)
        {
            std::string config_dir = join_path(*path, "Transmission");
            add_install_if_valid(config_dir, "official");
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

    // 4. Microsoft Store installation
    {
        auto path = get_known_folder_path(FOLDERID_LocalAppData);
        if (path)
        {
            std::string packages_root = join_path(*path, "Packages");
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
                        add_install_if_valid(config_dir, "msstore");
                    }
                    break; // Only need one MS Store install
                }
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
    DiscoveredInstall const& install,
    PortableOptions const& options)
{
    ImportResult result;
    result.success = false;

    if (install.config_dir.empty())
    {
        result.errors.emplace_back("Host install has no config directory");
        return result;
    }

    if (!dir_exists(install.config_dir))
    {
        result.errors.emplace_back("Host config directory does not exist: " + install.config_dir);
        return result;
    }

    // Validate the host config directory has actual data
    if (!is_valid_transmission_config(install.config_dir))
    {
        result.errors.emplace_back("Host config directory appears empty or invalid: " + install.config_dir);
        return result;
    }

    // Check for file locks first
    result.locked_files = checkFileLocks(install.config_dir);
    if (!result.locked_files.empty())
    {
        result.warnings.push_back("Some files may be locked by running processes");
        for (auto const& lock : result.locked_files)
        {
            result.warnings.push_back("  " + lock.process_name + " (PID: " + std::to_string(lock.process_id) + ") has locks on " + lock.file_path);
        }
    }

    // Target portable config directory
    std::string portable_config = options.portable_root + "\\data";
    if (!dir_exists(portable_config))
    {
        std::filesystem::create_directories(portable_config);
        result.warnings.emplace_back("Created portable config directory: " + portable_config);
    }

    namespace fs = std::filesystem;

    // Import settings.json
    if (options.import_settings)
    {
        std::string src_settings = install.config_dir + "\\settings.json";
        std::string dst_settings = portable_config + "\\settings.json";

        if (file_exists(src_settings))
        {
            try
            {
                fs::copy_file(src_settings, dst_settings, fs::copy_options::overwrite_existing);
                result.imported_torrents.push_back("settings.json");
            }
            catch (fs::filesystem_error const& e)
            {
                result.errors.push_back("Failed to copy settings.json: " + std::string(e.what()));
            }
        }
        else
        {
            result.warnings.push_back("settings.json not found in host config");
        }
    }

    // Import resume directory
    if (options.import_resume)
    {
        std::string src_resume = install.config_dir + "\\resume";
        std::string dst_resume = portable_config + "\\resume";

        if (dir_exists(src_resume))
        {
            size_t before = result.imported_torrents.size();
            copy_directory_recursive(src_resume, dst_resume, result, options);
            size_t after = result.imported_torrents.size();
            result.warnings.emplace_back("Imported " + std::to_string(after - before) + " resume file(s)");
        }
        else
        {
            result.warnings.push_back("resume directory not found in host config");
        }
    }

    // Import torrents directory
    if (options.import_torrent_files)
    {
        std::string src_torrents = install.config_dir + "\\torrents";
        std::string dst_torrents = portable_config + "\\torrents";

        if (dir_exists(src_torrents))
        {
            size_t before = result.imported_torrents.size();
            copy_directory_recursive(src_torrents, dst_torrents, result, options);
            size_t after = result.imported_torrents.size();
            result.warnings.emplace_back("Imported " + std::to_string(after - before) + " torrent file(s)");
        }
        else
        {
            result.warnings.push_back("torrents directory not found in host config");
        }
    }

    // Import blocklists if present
    std::string src_blocklists = install.config_dir + "\\blocklists";
    std::string dst_blocklists = portable_config + "\\blocklists";
    if (dir_exists(src_blocklists))
    {
        size_t before = result.imported_torrents.size();
        copy_directory_recursive(src_blocklists, dst_blocklists, result, options);
        size_t after = result.imported_torrents.size();
        result.warnings.emplace_back("Imported " + std::to_string(after - before) + " blocklist file(s)");
    }

    // Import geoip if present
    std::string src_geoip = install.config_dir + "\\geoip";
    std::string dst_geoip = portable_config + "\\geoip";
    if (dir_exists(src_geoip))
    {
        size_t before = result.imported_torrents.size();
        copy_directory_recursive(src_geoip, dst_geoip, result, options);
        size_t after = result.imported_torrents.size();
        result.warnings.emplace_back("Imported " + std::to_string(after - before) + " geoip file(s)");
    }

    result.success = result.errors.empty();
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
