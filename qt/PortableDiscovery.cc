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
#include <filesystem>
#include <string>
#include <vector>

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

} // anonymous namespace

std::vector<HostInstall> discoverHostInstalls()
{
    std::vector<HostInstall> installs;

    // Transmission 4.x Qt client uses %LOCALAPPDATA%\transmission for config
    std::string local_appdata = get_known_folder_path(FOLDERID_LocalAppData);
    if (!local_appdata.empty())
    {
        std::string config_dir = local_appdata + "\\transmission";
        if (dir_exists(config_dir))
        {
            HostInstall install;
            install.type = "official";
            install.config_dir = config_dir;
            // install_dir detection can be added in Phase 2
            installs.push_back(std::move(install));
        }
    }

    // Microsoft Store installation
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
                    {
                        HostInstall install;
                        install.type = "msstore";
                        install.config_dir = local_state + "\\config";
                        installs.push_back(std::move(install));
                        break; // Only need one
                    }
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