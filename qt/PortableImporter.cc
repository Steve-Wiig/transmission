// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "PortableImporter.h"

#include "PortableDiscovery.h"

#include <QtGlobal>

#if defined(Q_OS_WIN)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <restartmanager.h>
#include <shlobj.h>
#include <psapi.h>
#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>
#include <memory>
#include <cwchar>

namespace tr
{
namespace portable
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

bool dir_exists(std::string_view path)
{
    std::wstring wpath = utf8_to_wide(path);
    DWORD attrs = GetFileAttributesW(wpath.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

bool file_exists(std::string_view path)
{
    std::wstring wpath = utf8_to_wide(path);
    DWORD attrs = GetFileAttributesW(wpath.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
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

std::string normalize_path(std::string const& path)
{
    std::string result = path;
    std::replace(result.begin(), result.end(), '\\', '/');
    return result;
}

std::string drive_to_unc(std::string const& path)
{
    if (path.size() < 2 || path[1] != ':')
        return path;

    char drive_letter = std::toupper(path[0]);
    if (drive_letter < 'A' || drive_letter > 'Z')
        return path;

    std::wstring wpath = utf8_to_wide(path);
    wchar_t unc_path[MAX_PATH * 4];
    DWORD unc_size = std::size(unc_path);

    // WNetGetUniversalNameW requires the path to be a root directory
    std::wstring root_path = wpath;
    auto pos = root_path.find_first_of(L"/\\");
    if (pos != std::wstring::npos)
        root_path = root_path.substr(0, pos + 1);
    else
        root_path += L"\\";

    DWORD result = WNetGetUniversalNameW(root_path.c_str(), UNIVERSAL_NAME_INFO_LEVEL, unc_path, &unc_size);
    if (result == NO_ERROR)
    {
        UNIVERSAL_NAME_INFOW* info = reinterpret_cast<UNIVERSAL_NAME_INFOW*>(unc_path);
        std::string unc = wide_to_utf8(info->lpUniversalName);

        // Append the relative portion
        if (pos != std::wstring::npos)
        {
            std::string rel = wide_to_utf8(wpath.substr(pos + 1));
            std::replace(rel.begin(), rel.end(), '\\', '/');
            unc += "/";
            unc += rel;
        }
        return unc;
    }

    return path;
}

namespace
{
{
    std::vector<FileLockInfo> locked_files;

    // Files/directories to check for locks
    std::vector<std::wstring> resources;
    std::wstring wconfig = utf8_to_wide(config_dir);

    // Add config directory itself
    resources.push_back(wconfig + L"\\*");

    // Add common subdirectories
    static constexpr std::wstring_view subdirs[] = {
        L"resume",
        L"torrents",
        L"blocklists",
        L"geoip"
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
        // Restart Manager not available or failed to start session
        return locked_files;
    }

    // RAII guard for Restart Manager session
    struct RmSessionGuard {
        DWORD handle = 0;
        ~RmSessionGuard() { if (handle) RmEndSession(handle); }
    } session_guard{session_handle};

    // Register resources
    UINT nApplications = 0;

    // Convert vector<wstring> to array of const WCHAR*
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

                // Get process name
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

                // Check which specific files this process has open
                // We'll report the config directory as potentially locked
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

ImportResult stageImport(HostInstall const& host_install, std::string const& portable_root, ImportOptions const& options)
{
    ImportResult result;

    if (host_install.config_dir.empty())
    {
        result.errors.push_back("Host install has no config directory");
        result.success = false;
        return result;
    }

    if (!dir_exists(host_install.config_dir))
    {
        result.errors.push_back("Host config directory does not exist: " + host_install.config_dir);
        result.success = false;
        return result;
    }

    // Check for file locks first
    result.locked_files = checkFileLocks(host_install.config_dir);
    if (!result.locked_files.empty())
    {
        result.warnings.push_back("Some files may be locked by running processes");
        for (auto const& lock : result.locked_files)
        {
            result.warnings.push_back("  " + lock.process_name + " (PID: " + std::to_string(lock.process_id) + ") has locks on " + lock.file_path);
        }
    }

    // Target portable config directory
    std::string portable_config = portable_root + "\\data";
    if (!dir_exists(portable_config))
    {
        std::filesystem::create_directories(portable_config);
    }

    namespace fs = std::filesystem;

    // Import settings.json
    if (options.import_settings)
    {
        std::string src_settings = host_install.config_dir + "\\settings.json";
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
        std::string src_resume = host_install.config_dir + "\\resume";
        std::string dst_resume = portable_config + "\\resume";

        if (dir_exists(src_resume))
        {
            copy_directory_recursive(src_resume, dst_resume, result, options);
        }
        else
        {
            result.warnings.push_back("resume directory not found in host config");
        }
    }

    // Import torrents directory
    if (options.import_torrent_files)
    {
        std::string src_torrents = host_install.config_dir + "\\torrents";
        std::string dst_torrents = portable_config + "\\torrents";

        if (dir_exists(src_torrents))
        {
            copy_directory_recursive(src_torrents, dst_torrents, result, options);
        }
        else
        {
            result.warnings.push_back("torrents directory not found in host config");
        }
    }

    // Import blocklists if present
    std::string src_blocklists = host_install.config_dir + "\\blocklists";
    std::string dst_blocklists = portable_config + "\\blocklists";
    if (dir_exists(src_blocklists))
    {
        copy_directory_recursive(src_blocklists, dst_blocklists, result, options);
    }

    // Import geoip if present
    std::string src_geoip = host_install.config_dir + "\\geoip";
    std::string dst_geoip = portable_config + "\\geoip";
    if (dir_exists(src_geoip))
    {
        copy_directory_recursive(src_geoip, dst_geoip, result, options);
    }

    result.success = result.errors.empty();
    return result;
}

} // namespace portable
} // namespace tr

#else // !Q_OS_WIN

namespace tr
{
namespace portable
{

std::vector<FileLockInfo> checkFileLocks(std::string const& /*config_dir*/)
{
    return {};
}

ImportResult stageImport(HostInstall const& /*host_install*/, std::string const& /*portable_root*/, ImportOptions const& /*options*/)
{
    ImportResult result;
    result.success = true;
    result.warnings.push_back("portable-import is currently stubbed for non-Windows builds");
    return result;
}

std::string normalize_path(std::string const& path)
{
    return path;
}

std::string drive_to_unc(std::string const& path)
{
    return path;
}

} // namespace portable
} // namespace tr

#endif // Q_OS_WIN
