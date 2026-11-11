// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <memory>
#include <string_view>
#include <filesystem>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QStandardPaths>
#include <cstdio>

#include <fmt/format.h>

#include <libtransmission/transmission.h>

#include <libtransmission/tr-getopt.h>
#include <libtransmission/utils.h>
#include <libtransmission/version.h>
#include <libtransmission/portable-discovery.h>
#include <libtransmission/log.h>

#include <libtransmission-app/app.h>

#include "Application.h"
#include "InteropHelper.h"
#include "PortableDiscovery.h"
#include "PortableImporter.h"
#include "PortableRemap.h"
#include "Prefs.h"
#include "RpcClient.h"
#include "VariantHelpers.h"

using namespace std::string_view_literals;

namespace
{

#ifdef Q_OS_WIN
QString getExeDir()
{
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
    wchar_t path[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (len == 0 || len == MAX_PATH)
        return {};
    QString exe_path = QString::fromWCharArray(path, len);
    return QFileInfo(exe_path).absolutePath();
}
#else
QString getExeDir()
{
    return QCoreApplication::applicationDirPath();
}
#endif

char const* const DisplayName = "transmission-qt";

auto constexpr FileArgsSeparator = "--"sv;
auto constexpr QtArgsSeparator = "---"sv;

using Arg = tr_option::Arg;
auto constexpr Opts = std::array<tr_option, 10>{ {
    {
        .val = 'g',
        .longName = "config-dir",
        .description = "Where to look for configuration files",
        .shortName = "g",
        .arg = Arg::Required,
        .argName = "<path>",
    },
    {
        .val = 'm',
        .longName = "minimized",
        .description = "Start minimized in system tray",
        .shortName = "m",
        .arg = Arg::None,
        .argName = nullptr,
    },
    {
        .val = 'p',
        .longName = "port",
        .description = "Port to use when connecting to an existing session",
        .shortName = "p",
        .arg = Arg::Required,
        .argName = "<port>",
    },
    {
        .val = 'r',
        .longName = "remote",
        .description = "Connect to an existing session at the specified hostname",
        .shortName = "r",
        .arg = Arg::Required,
        .argName = "<host>",
    },
    {
        .val = 'u',
        .longName = "username",
        .description = "Username to use when connecting to an existing session",
        .shortName = "u",
        .arg = Arg::Required,
        .argName = "<username>",
    },
    {
        .val = 'v',
        .longName = "version",
        .description = "Show version number and exit",
        .shortName = "v",
        .arg = Arg::None,
        .argName = nullptr,
    },
    {
        .val = 'w',
        .longName = "password",
        .description = "Password to use when connecting to an existing session",
        .shortName = "w",
        .arg = Arg::Required,
        .argName = "<password>",
    },
    {
        .val = 'P',
        .longName = "portable",
        .description = "Run in portable mode (store config next to executable)",
        .shortName = "P",
        .arg = Arg::None,
        .argName = nullptr,
    },
    {
        .val = 'R',
        .longName = "portable-reimport",
        .description = "Force re-import from host installation in portable mode",
        .shortName = "R",
        .arg = Arg::None,
        .argName = nullptr,
    },
    {
        .val = 0,
        .longName = nullptr,
        .description = nullptr,
        .shortName = nullptr,
        .arg = Arg::None,
        .argName = nullptr,
    },
} };
static_assert(Opts[std::size(Opts) - 2].val != 0);
} // namespace

namespace
{
char const* getUsage()
{
    return "Usage:\n"
           "  transmission-qt [options...] [[--] torrent files...] [--- Qt options...]";
}

bool tryDelegate(QStringList const& filenames)
{
    InteropHelper const interop_client;
    if (!interop_client.isConnected())
    {
        return false;
    }

    bool delegated = false;

    for (auto const& filename : filenames)
    {
        auto const add_data = AddData(filename);
        QString metainfo;

        switch (add_data.type)
        {
        case AddData::URL:
            metainfo = add_data.url.toString();
            break;

        case AddData::MAGNET:
            metainfo = add_data.magnet;
            break;

        case AddData::FILENAME:
        case AddData::METAINFO:
            metainfo = QString::fromUtf8(add_data.toBase64());
            break;

        default:
            break;
        }

        if (!metainfo.isEmpty() && interop_client.addMetainfo(metainfo))
        {
            delegated = true;
        }
    }

    return delegated;
}

} // namespace

static void trqMessageHandler(QtMsgType type, QMessageLogContext const&, QString const& msg)
{
    static QFile f{ QDir{ QStandardPaths::writableLocation(QStandardPaths::TempLocation) }.filePath("trq-debug.log") };
    if (!f.isOpen()) { f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text); }
    if (f.isOpen()) {
        f.write(msg.toUtf8());
        f.write("\n");
        f.flush();
    }
    fprintf(stderr, "%s\n", qUtf8Printable(msg));
}

int tr_main(int argc, char** argv)
{
    qInstallMessageHandler(trqMessageHandler);
    tr::app::init();
    trqt::variant_helpers::register_qt_converters();

    // Enable INFO level logging early for portable mode debugging
    tr_logSetLevel(TR_LOG_INFO);

    // parse the command-line arguments
    bool minimized = false;
    bool portable = false;
    bool portable_reimport = false;
    QString host;
    QString port;
    QString username;
    QString password;
    QString config_dir;
    QStringList filenames;

    int opt = 0;
    char const* arg = nullptr;
    int file_args_start_idx = -1;
    int qt_args_start_idx = -1;
    while (file_args_start_idx < 0 && qt_args_start_idx < 0 &&
           (opt = tr_getopt(getUsage(), argc, static_cast<char const* const*>(argv), std::data(Opts), &arg)) != TR_OPT_DONE)
    {
        switch (opt)
        {
        case 'g':
            config_dir = QString::fromUtf8(arg);
            break;

        case 'p':
            port = QString::fromUtf8(arg);
            break;

        case 'r':
            host = QString::fromUtf8(arg);
            break;

        case 'u':
            username = QString::fromUtf8(arg);
            break;

        case 'w':
            password = QString::fromUtf8(arg);
            break;

        case 'P':
            portable = true;
            break;

        case 'R':
            portable_reimport = true;
            break;

        case 'm':
            minimized = true;
            break;

        case 'v':
            fmt::print("{:s} {:s}\n", DisplayName, LONG_VERSION_STRING);
            return 0;

        case TR_OPT_ERR:
            fmt::print(stderr, "Invalid option\n");
            tr_getopt_usage(DisplayName, getUsage(), std::data(Opts));
            return 1;

        default:
            if (arg == FileArgsSeparator)
            {
                file_args_start_idx = tr_optind;
            }
            else if (arg == QtArgsSeparator)
            {
                qt_args_start_idx = tr_optind;
            }
            else if (std::string_view{arg} == "portable")
            {
                portable = true;
            }
            else
            {
                filenames.append(QString::fromUtf8(arg));
            }

            break;
        }
    }

    if (file_args_start_idx >= 0)
    {
        for (int i = file_args_start_idx; i < argc; ++i)
        {
            if (argv[i] == QtArgsSeparator)
            {
                qt_args_start_idx = i + 1;
                break;
            }

            filenames.push_back(QString::fromUtf8(argv[i]));
        }
    }

    // Handle portable mode early - before prefs initialization
    if (portable)
    {
        tr_logAddInfo("[PortableMode] Portable mode enabled");

        // Get executable directory for portable config
        QString exe_dir = getExeDir();
        std::string portable_root = exe_dir.toStdString();
        std::string portable_config = portable_root + "/data";

        tr_logAddInfo("[PortableMode] Portable root: " + portable_root);
        tr_logAddInfo("[PortableMode] Portable config: " + portable_config);

        // Check if portable config already exists with data
        std::string portable_settings = portable_config + "/settings.json";
        bool portable_exists = std::filesystem::exists(portable_settings);

        // Debug: write portable check results
        {
            std::ofstream dbg(portable_root + "/portable_check.txt");
            dbg << "portable_config: " << portable_config << "\n";
            dbg << "portable_settings: " << portable_settings << "\n";
            dbg << "portable_exists: " << portable_exists << "\n";
            dbg << "portable_reimport: " << portable_reimport << "\n";
            dbg << "data dir exists: " << std::filesystem::exists(portable_config) << "\n";
        }

        if (portable_exists && !portable_reimport)
        {
            tr_logAddInfo("[PortableMode] Using existing portable config");
            config_dir = QString::fromStdString(portable_config);
            qputenv("TRANSMISSION_HOME", config_dir.toUtf8());
        }
        else
        {
            if (portable_reimport)
            {
                tr_logAddInfo("[PortableMode] Forced re-import from host installation (--portable-reimport)");
            }
            else
            {
                tr_logAddInfo("[PortableMode] No existing portable config, importing from host installation...");
            }

            // Discover host Transmission installations using core library
            auto host_installs = tr::discover_installations();
            tr_logAddInfo("[PortableMode] Found " + std::to_string(host_installs.size()) + " host installation(s)");

            bool imported = false;
            for (auto const& host_install : host_installs)
            {
                tr_logAddInfo("[PortableMode] Checking host install: " + host_install.config_root + " (type: " + std::to_string(static_cast<int>(host_install.type)) + ")");

                if (host_install.config_root.empty() || !std::filesystem::exists(host_install.config_root))
                {
                    tr_logAddInfo("[PortableMode]   Skipping - invalid config directory");
                    continue;
                }

                // Validate the host config directory has actual data
                std::string resume_dir = host_install.config_root + "/resume";
                std::string torrents_dir = host_install.config_root + "/torrents";
                std::string settings_file = host_install.config_root + "/settings.json";
                
                bool has_settings = std::filesystem::exists(settings_file);
                bool has_resume = false;
                bool has_torrents = false;
                
                if (std::filesystem::exists(resume_dir))
                {
                    for (auto const& entry : std::filesystem::directory_iterator(resume_dir))
                    {
                        if (entry.is_regular_file() && entry.path().extension() == ".resume")
                        {
                            has_resume = true;
                            break;
                        }
                    }
                }
                
                if (std::filesystem::exists(torrents_dir))
                {
                    for (auto const& entry : std::filesystem::directory_iterator(torrents_dir))
                    {
                        if (entry.is_regular_file() && 
                            (entry.path().extension() == ".torrent" || entry.path().extension() == ".magnet"))
                        {
                            has_torrents = true;
                            break;
                        }
                    }
                }
                
                if (!has_settings || (!has_resume && !has_torrents))
                {
                    tr_logAddInfo("[PortableMode]   Skipping - no valid Transmission data");
                    continue;
                }

                // Import from host installation to portable config
                tr_logAddInfo("[PortableMode] Importing from host install...");

                tr::PortableOptions options;
                options.portable_root = portable_root;
                                options.import_settings = true;
                options.import_resume = true;
                options.remap_drive_to_unc = true;
                options.import_torrent_files = true;
                options.remap_drive_to_unc = true;
                options.fail_fast = false;

                tr::ImportResult result = tr::import_from_install(host_install, options);

                if (result.success)
                {
                    tr_logAddInfo("[PortableMode] Import succeeded! Imported " + std::to_string(result.imported_torrents.size()) + " file(s)");
                    if (!result.warnings.empty())
                    {
                        for (auto const& w : result.warnings)
                            tr_logAddInfo("[PortableMode]   Warning: " + w);
                    }
                    if (!result.locked_files.empty())
                    {
                        for (auto const& l : result.locked_files)
                            tr_logAddInfo("[PortableMode]   Locked: " + l.process_name + " (PID: " + std::to_string(l.process_id) + ")");
                    }

                    config_dir = QString::fromStdString(portable_config);
                    qputenv("TRANSMISSION_HOME", config_dir.toUtf8());
                    imported = true;
                    break; // Use first valid host install
                }
                else
                {
                    std::cout << "[PortableMode] Import failed:" << std::endl;
                    for (auto const& e : result.errors)
                        std::cout << "[PortableMode]   Error: " << e << std::endl;
                }
            }

            if (!imported)
            {
                // Fallback: create empty portable config dir next to exe
                std::cout << "[PortableMode] No host installation imported, creating empty portable config at: " << portable_config << std::endl;
                std::filesystem::create_directories(portable_config);
                config_dir = QString::fromStdString(portable_config);
                qputenv("TRANSMISSION_HOME", config_dir.toUtf8());
            }
        }

        std::cout << "[PortableMode] config_dir set to: " << config_dir.toStdString() << std::endl;
    }

    // In portable mode, disable COM interop initialization and delegation
    // to avoid handing off to the installed host instance
    if (!portable)
    {
        InteropHelper::initialize();

        // try to delegate the work to an existing copy of Transmission
        // before starting ourselves...
        if (tryDelegate(filenames))
        {
            return 0;
        }
    }

    // set the fallback config dir
    if (config_dir.isNull())
    {
        config_dir = QString::fromStdString(tr_getDefaultConfigDir("transmission"));
    }

    // initialize the prefs
    auto prefs = Prefs{ config_dir };

    if (!host.isNull())
    {
        prefs.set(TR_KEY_remote_session_host, host);
    }

    if (!port.isNull())
    {
        prefs.set(TR_KEY_remote_session_port, port.toUInt());
    }

    if (!username.isNull())
    {
        prefs.set(TR_KEY_remote_session_username, username);
    }

    if (!password.isNull())
    {
        prefs.set(TR_KEY_remote_session_password, password);
    }

    if (!host.isNull() || !port.isNull() || !username.isNull() || !password.isNull())
    {
        prefs.set(TR_KEY_remote_session_enabled, true);
    }

    if (prefs.get<bool>(TR_KEY_start_minimized))
    {
        minimized = true;
    }

    // start as minimized only if the system tray present
    if (!prefs.get<bool>(TR_KEY_show_notification_area_icon))
    {
        minimized = false;
    }

    auto qt_argv = std::vector<char*>{ argv[0] };
    if (qt_args_start_idx >= 0)
    {
        qt_argv.insert(qt_argv.end(), &argv[qt_args_start_idx], &argv[argc]);
    }

    // run the app
    auto qt_argc = static_cast<int>(std::size(qt_argv));
    auto nam = QNetworkAccessManager{};
    auto rpc = RpcClient{ nam };
    auto const app = Application{ prefs, rpc, minimized, config_dir, filenames, qt_argc, std::data(qt_argv), portable };
    auto const ret = QApplication::exec();

    // save prefs before exiting
    auto const filename = QDir{ config_dir }.absoluteFilePath(QStringLiteral("settings.json"));
    prefs.save(filename);

    return ret;
}
