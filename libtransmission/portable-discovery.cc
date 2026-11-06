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
#include <winnetwk.h> // WNetGetUniversalNameW
#include <restartmanager.h> // RmStartSession, etc.
#include <psapi.h> // GetModuleFileNameExW
#include <strsafe.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "mpr.lib")
#endif

#include <filesystem>
#include <fstream>
#include <sstream>

#include <libtransmission/file.h>
#include <libtransmission/file-utils.h>
#include <libtransmission/serializer.h>
#include <libtransmission/variant.h>
#include <libtransmission/quark.h>

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

// Helper to convert UTF-8 to wide string
std::wstring utf8_to_wide(std::string_view str)
{
    if (str.empty())
    {
        return {};
    }
    int size = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
    if (size <= 0)
    {
        return {};
    }
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), size);
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

// Convert drive letter path to UNC path (e.g., Z:\path -> \\server\share\path)
std::string drive_to_unc_impl(std::string const& path)
{
    if (path.size() < 2 || path[1] != ':')
        return path;

    std::wstring wpath = utf8_to_wide(path);
    
    // Get the root path (e.g., "Z:\")
    std::wstring root_path = wpath.substr(0, 3);
    if (root_path.size() == 2) // "Z:" -> "Z:\"
        root_path += L"\\";

    wchar_t unc_path[MAX_PATH * 4];
    DWORD unc_size = sizeof(unc_path);

    DWORD result = WNetGetUniversalNameW(root_path.c_str(), UNIVERSAL_NAME_INFO_LEVEL, unc_path, &unc_size);
    if (result == NO_ERROR)
    {
        UNIVERSAL_NAME_INFOW* info = reinterpret_cast<UNIVERSAL_NAME_INFOW*>(unc_path);
        std::string unc = wide_to_utf8(info->lpUniversalName);

        // Append the relative portion after the drive letter
        if (wpath.size() > 3)
        {
            std::string rel = wide_to_utf8(wpath.substr(3));
            std::replace(rel.begin(), rel.end(), '\\', '/');
            unc += "/";
            unc += rel;
        }
        return unc;
    }

    return path;
}

// Check for file locks using Restart Manager
std::vector<FileLockInfo> checkFileLocks(std::string const& config_dir)
{
    std::vector<FileLockInfo> locked_files;

    std::wstring wconfig = utf8_to_wide(config_dir);

    // Files/directories to check for locks
    std::vector<std::wstring> resources;
    resources.push_back(wconfig + L"\\*");

    static constexpr std::wstring_view subdirs[] = {
        L"resume", L"torrents", L"blocklists", L"geoip"
    };

    for (auto const& sub : subdirs)
    {
        resources.push_back(wconfig + L"\\" + std::wstring(sub) + L"\\*");
    }

    DWORD session_handle = 0;
    WCHAR session_key[CCH_RM_SESSION_KEY + 1] = {0};
    DWORD result = RmStartSession(&session_handle, 0, session_key);
    if (result != ERROR_SUCCESS)
    {
        return locked_files;
    }

    struct RmSessionGuard {
        DWORD handle = 0;
        ~RmSessionGuard() { if (handle) RmEndSession(handle); }
    } session_guard{session_handle};

    // Register resources
    std::vector<const WCHAR*> resource_ptrs;
    resource_ptrs.reserve(resources.size());
    for (auto const& r : resources)
        resource_ptrs.push_back(r.c_str());

    result = RmRegisterResources(session_handle,
                                 static_cast<UINT>(resource_ptrs.size()),
                                 resource_ptrs.data(),
                                 0, nullptr,
                                 0, nullptr);
    if (result != ERROR_SUCCESS)
    {
        return locked_files;
    }

    // Get the list of processes locking the resources
    UINT nProcInfoNeeded = 0;
    std::vector<RM_PROCESS_INFO> rgProcessInfo;
    DWORD reboot_reasons = 0;
    UINT nApplications = 0;
    result = RmGetList(session_handle, &nProcInfoNeeded, &nApplications, nullptr, &reboot_reasons);
    if (result == ERROR_MORE_DATA && nApplications > 0)
    {
        rgProcessInfo.resize(nApplications);
        result = RmGetList(session_handle, &nProcInfoNeeded, &nApplications, rgProcessInfo.data(), &reboot_reasons);
        if (result == ERROR_SUCCESS)
        {
            for (UINT i = 0; i < nApplications; ++i)
            {
                RM_PROCESS_INFO const& proc_info = rgProcessInfo[i];
                RM_UNIQUE_PROCESS const& proc = proc_info.Process;

                HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, proc.dwProcessId);
                std::string process_name = "Unknown";
                if (hProcess)
                {
                    WCHAR exe_path[MAX_PATH * 4];
                    DWORD exe_size = std::size(exe_path);
                    if (GetModuleFileNameExW(hProcess, nullptr, exe_path, exe_size) > 0)
                    {
                        std::wstring wpath(exe_path);
                        auto pos = wpath.find_last_of(L"\\/");
                        if (pos != std::wstring::npos)
                            wpath = wpath.substr(pos + 1);
                        process_name = wide_to_utf8(wpath);
                    }
                    CloseHandle(hProcess);
                }

                FileLockInfo info;
                info.process_name = process_name;
                info.process_id = proc.dwProcessId;
                info.file_path = config_dir;
                locked_files.push_back(info);
            }
        }
    }

    return locked_files;
}

#else // !_WIN32

std::string drive_to_unc_impl(std::string const& path)
{
    return path;
}

std::vector<FileLockInfo> checkFileLocks(std::string const& /*config_dir*/)
{
    return {};
}

#endif // _WIN32

// Helper to validate a Transmission config directory - checks for non-empty dirs
bool is_valid_transmission_config(std::string_view path)
{
    std::string resume_dir = std::string(path) + "/resume";
    std::string torrents_dir = std::string(path) + "/torrents";
    std::string settings_file = std::string(path) + "/settings.json";
    
    if (!file_exists(settings_file))
        return false;
        
    if (dir_exists(resume_dir)) {
        for (auto const& entry : std::filesystem::directory_iterator(resume_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".resume") {
                return true;
            }
        }
    }
    
    if (dir_exists(torrents_dir)) {
        for (auto const& entry : std::filesystem::directory_iterator(torrents_dir)) {
            if (entry.is_regular_file() && 
                (entry.path().extension() == ".torrent" || entry.path().extension() == ".magnet")) {
                return true;
            }
        }
    }
    
    return false;
}

// Recursively copy directory
void copy_directory_recursive(std::string const& src, std::string const& dst, ImportResult& result, PortableOptions const& options)
{
    namespace fs = std::filesystem;
    try
    {
        for (auto const& entry : fs::recursive_directory_iterator(src))
        {
            auto rel_path = fs::relative(entry.path(), src);
            auto dst_path = fs::path(dst) / rel_path;

            if (entry.is_directory())
            {
                fs::create_directories(dst_path);
            }
            else if (entry.is_regular_file())
            {
                fs::copy_file(entry.path(), dst_path, fs::copy_options::overwrite_existing);
                result.imported_torrents.push_back(normalize_path(rel_path.string()));
            }
        }
    }
    catch (fs::filesystem_error const& e)
    {
        result.errors.push_back("Failed to copy " + normalize_path(src) + " to " + normalize_path(dst) + ": " + std::string(e.what()));
        if (options.fail_fast)
            throw;
    }
}

// Remap paths in settings.json using drive_to_unc
bool migrateSettings(std::string const& settingsPath, bool remap_drive_to_unc)
{
    // Parse settings.json using JSON serde
    auto serde = tr_variant_serde::json();
    auto opt_var = serde.parse_file(settingsPath);
    if (!opt_var)
    {
        return false;
    }

    auto var = std::move(*opt_var);
    auto* map = var.get_if<tr_variant::Map>();
    if (!map)
    {
        return false;
    }

    bool modified = false;

    // Settings keys that contain paths needing remapping
    struct SettingsPathKey
    {
        tr_quark key;
        bool is_directory = true;
    };

    static constexpr SettingsPathKey settings_path_keys[] = {
        { TR_KEY_download_dir, true },
        { TR_KEY_incomplete_dir, true },
        { TR_KEY_watch_dir, true },
        { TR_KEY_script_torrent_done_filename, false },
        { TR_KEY_script_torrent_done_seeding_filename, false },
        { TR_KEY_script_torrent_added_filename, false },
    };

    // Settings keys to disable for security (portable mode should not run host scripts)
    static constexpr tr_quark settings_disable_keys[] = {
        TR_KEY_watch_dir_enabled,
        TR_KEY_script_torrent_added_enabled,
        TR_KEY_script_torrent_done_enabled,
        TR_KEY_script_torrent_done_seeding_enabled,
    };

    // Remap path values
    if (remap_drive_to_unc)
    {
        for (auto const& key_info : settings_path_keys)
        {
            if (auto const iter = map->find(key_info.key); iter != map->end())
            {
                if (auto const opt = iter->second.value_if<std::string_view>(); opt)
                {
                    std::string path = *opt;
                    std::string remapped = drive_to_unc_impl(path);
                    if (remapped != path)
                    {
                        map->insert_or_assign(key_info.key, remapped);
                        modified = true;
                    }
                }
            }
        }
    }

    // Security sanitization: disable dangerous automation
    for (auto const& key : settings_disable_keys)
    {
        if (auto const iter = map->find(key); iter != map->end())
        {
            if (auto const opt = iter->second.value_if<bool>(); opt && *opt)
            {
                map->insert_or_assign(key, false);
                modified = true;
            }
        }
        else
        {
            map->insert_or_assign(key, false);
            modified = true;
        }
    }

    // Also ensure watch-dir is empty if watch-dir-enabled is disabled
    if (auto const iter = map->find(TR_KEY_watch_dir); iter != map->end())
    {
        if (auto const opt = iter->second.value_if<std::string_view>(); opt && !opt->empty())
        {
            map->insert_or_assign(TR_KEY_watch_dir, "");
            modified = true;
        }
    }

    if (!modified)
    {
        return true; // Nothing to change
    }

    // Write back
    auto const json_str = serde.to_string(var);
    std::ofstream out(settingsPath, std::ios::binary);
    if (!out)
        return false;
    out.write(json_str.data(), json_str.size());
    return true;
}

// Remap paths in resume files using drive_to_unc
bool migrateResumeFiles(std::string const& resumeDir, bool remap_drive_to_unc)
{
    if (!dir_exists(resumeDir))
        return false;

    auto serde = tr_variant_serde::benc();
    bool any_modified = false;

    try
    {
        for (auto const& entry : std::filesystem::directory_iterator(resumeDir))
        {
            if (!entry.is_regular_file() || entry.path().extension() != ".resume")
                continue;

            std::string filePath = entry.path().string();

            // Parse .resume file using bencode serde
            auto opt_var = serde.parse_file(filePath);
            if (!opt_var)
            {
                // Corrupt resume file - skip but don't fail entirely
                continue;
            }

            auto var = std::move(*opt_var);
            auto* map = var.get_if<tr_variant::Map>();
            if (!map)
                continue;

            bool modified = false;

            // Resume file keys that contain paths needing remapping
            static constexpr tr_quark resume_path_keys[] = {
                TR_KEY_destination,
                TR_KEY_incomplete_dir,
            };

            if (remap_drive_to_unc)
            {
                for (auto const& key : resume_path_keys)
                {
                    if (auto const iter = map->find(key); iter != map->end())
                    {
                        if (auto const opt = iter->second.value_if<std::string_view>(); opt)
                        {
                            std::string path = *opt;
                            std::string remapped = drive_to_unc_impl(path);
                            if (remapped != path)
                            {
                                map->insert_or_assign(key, remapped);
                                modified = true;
                            }
                        }
                    }
                }
            }

            if (!modified)
                continue;

            any_modified = true;

            // Write back
            auto const benc_str = serde.to_string(var);
            std::ofstream out(filePath, std::ios::binary);
            if (!out)
                continue;
            out.write(benc_str.data(), benc_str.size());
        }
    }
    catch (std::filesystem::filesystem_error const&)
    {
        // Ignore filesystem errors during migration
    }

    return any_modified;
}

// Build a DiscoveredInstall from an install root path
std::optional<DiscoveredInstall> make_discovered_install(std::string install_root, InstallType type)
{
    install_root = normalize_path(install_root);

    if (!dir_exists(install_root))
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
        #ifdef _WIN32
        wchar_t* local_appdata = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local_appdata)))
        {
            install.config_root = wide_to_utf8(local_appdata) + "\\transmission";
            CoTaskMemFree(local_appdata);
        }
        else
        #endif
        {
            install.config_root = install_root; // Fallback
        }
    }
    else if (type == InstallType::MicrosoftStore)
    {
        // MS Store apps use LocalState\config
        install.config_root = join_path(install_root, "LocalState\\config");
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
    install.has_settings = file_exists(join_path(install.config_root, "settings.json"));

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
    if (!file_exists(exe_path))
    {
        exe_path = join_path(install_root, "transmission-qt.exe");
    }
    if (!file_exists(exe_path))
    {
        exe_path = join_path(install_root, "transmission-gtk.exe");
    }
    if (file_exists(exe_path))
    {
        install.exe_path = exe_path;
    }

    return install;
}

} // namespace

std::vector<DiscoveredInstall> discover_installations()
{
    std::vector<DiscoveredInstall> results;

#ifdef _WIN32
    // Helper to validate Transmission config directory
    auto is_valid_transmission_config = [](std::string_view path) -> bool {
        std::string resume_dir = std::string(path) + "\\resume";
        std::string torrents_dir = std::string(path) + "\\torrents";
        std::string settings_file = std::string(path) + "\\settings.json";
        
        if (!file_exists(settings_file))
            return false;
            
        if (dir_exists(resume_dir)) {
            for (auto const& entry : std::filesystem::directory_iterator(resume_dir)) {
                if (entry.is_regular_file() && entry.path().extension() == ".resume") {
                    return true;
                }
            }
        }
        
        if (dir_exists(torrents_dir)) {
            for (auto const& entry : std::filesystem::directory_iterator(torrents_dir)) {
                if (entry.is_regular_file() && 
                    (entry.path().extension() == ".torrent" || entry.path().extension() == ".magnet")) {
                    return true;
                }
            }
        }
        
        return false;
    };

    auto add_install_if_valid = [&results, &is_valid_transmission_config](std::string const& config_dir, InstallType type) {
        if (dir_exists(config_dir) && is_valid_transmission_config(config_dir)) {
            bool found = false;
            for (auto const& inst : results) {
                if (inst.config_root == config_dir) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                DiscoveredInstall install;
                install.type = type;
                install.config_root = config_dir;
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
            add_install_if_valid(config_dir, InstallType::Official);
        }
    }

    // %APPDATA%\Transmission (Roaming)
    {
        auto path = get_known_folder_path(FOLDERID_RoamingAppData);
        if (path)
        {
            std::string config_dir = join_path(*path, "Transmission");
            add_install_if_valid(config_dir, InstallType::Official);
        }
    }

    // %PROGRAMDATA%\Transmission (All users)
    {
        auto path = get_known_folder_path(FOLDERID_ProgramData);
        if (path)
        {
            std::string config_dir = join_path(*path, "Transmission");
            add_install_if_valid(config_dir, InstallType::Official);
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
                        add_install_if_valid(config_dir, InstallType::MicrosoftStore);
                    }
                    break; // Only need one MS Store install
                }
            }
        }
    }

    // Sort by priority: Official > Portable > MicrosoftStore > DaemonService > Unknown
    // Within same type, prefer ones with more complete data
    std::sort(results.begin(), results.end(),
        [](DiscoveredInstall const& a, DiscoveredInstall const& b)
        {
            if (static_cast<int>(a.type) != static_cast<int>(b.type))
            {
                return static_cast<int>(a.type) < static_cast<int>(b.type);
            }
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

bool has_portable_marker(std::string const& exe_path)
{
    std::string marker_path = join_path(dir_name(exe_path), "portable.dat");
    return file_exists(marker_path);
}

ImportResult import_from_install(
    DiscoveredInstall const& install,
    PortableOptions const& options)
{
    ImportResult result;
    result.success = false;

    if (install.config_root.empty())
    {
        result.errors.emplace_back("Host install has no config directory");
        return result;
    }

    if (!dir_exists(install.config_root))
    {
        result.errors.emplace_back("Host config directory does not exist: " + install.config_root);
        return result;
    }

    // Validate the host config directory has actual data
    if (!is_valid_transmission_config(install.config_root))
    {
        result.errors.emplace_back("Host config directory appears empty or invalid: " + install.config_root);
        return result;
    }

    // Check for file locks first
    result.locked_files = checkFileLocks(install.config_root);
    if (!result.locked_files.empty())
    {
        result.warnings.push_back("Some files may be locked by running processes");
        for (auto const& lock : result.locked_files)
        {
            result.warnings.push_back("  " + lock.process_name + " (PID: " + std::to_string(lock.process_id) + ") has locks on " + lock.file_path);
        }
    }

    // Target portable config directory
    std::string portable_config = join_path(options.portable_root, "data");
    if (!dir_exists(portable_config))
    {
        std::filesystem::create_directories(portable_config);
        result.warnings.emplace_back("Created portable config directory: " + portable_config);
    }

    // Import settings.json
    if (options.import_settings)
    {
        std::string src_settings = join_path(install.config_root, "settings.json");
        std::string dst_settings = join_path(portable_config, "settings.json");

        if (file_exists(src_settings))
        {
            try
            {
                std::filesystem::copy_file(src_settings, dst_settings, std::filesystem::copy_options::overwrite_existing);
                
                // Remap paths in the copied settings.json
                migrateSettings(dst_settings, options.remap_drive_to_unc);
                
                result.imported_torrents.push_back("settings.json");
            }
            catch (std::filesystem::filesystem_error const& e)
            {
                result.errors.push_back("Failed to copy settings.json: " + std::string(e.what()));
                if (options.fail_fast)
                    return result;
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
        std::string src_resume = join_path(install.config_root, "resume");
        std::string dst_resume = join_path(portable_config, "resume");

        if (dir_exists(src_resume))
        {
            size_t before = result.imported_torrents.size();
            copy_directory_recursive(src_resume, dst_resume, result, options);
            
            // Remap paths in resume files
            migrateResumeFiles(dst_resume, options.remap_drive_to_unc);
            
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
        std::string src_torrents = join_path(install.config_root, "torrents");
        std::string dst_torrents = join_path(portable_config, "torrents");

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
    std::string src_blocklists = join_path(install.config_root, "blocklists");
    std::string dst_blocklists = join_path(portable_config, "blocklists");
    if (dir_exists(src_blocklists))
    {
        size_t before = result.imported_torrents.size();
        copy_directory_recursive(src_blocklists, dst_blocklists, result, options);
        size_t after = result.imported_torrents.size();
        result.warnings.emplace_back("Imported " + std::to_string(after - before) + " blocklist file(s)");
    }

    // Import geoip if present
    std::string src_geoip = join_path(install.config_root, "geoip");
    std::string dst_geoip = join_path(portable_config, "geoip");
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
    return drive_to_unc_impl(path);
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
