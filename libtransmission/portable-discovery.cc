#include "libtransmission/portable-discovery.h"

#include <algorithm>
#include <string>
#include <string_view>

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

} // namespace

std::vector<DiscoveredInstall> discover_installations()
{
    return {};
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
    result.warnings.emplace_back("portable-discovery is currently stubbed for compilation");
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
