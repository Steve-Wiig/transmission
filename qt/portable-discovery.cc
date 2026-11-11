// This file Copyright (c) Transmission contributors.
#include "portable-discovery.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDebug>

#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace
{
QString officialConfigDirectory()
{
#ifdef Q_OS_WIN
    auto const appdata = qEnvironmentVariable("APPDATA");
    if (!appdata.isEmpty())
    {
        auto const candidate = QDir{ appdata }.absoluteFilePath(QStringLiteral("Transmission"));
        if (QFileInfo{ candidate }.isDir())
        {
            return QDir::cleanPath(candidate);
        }
    }
#endif
    return {};
}

QString markerConfigDirectory(QString const& marker_path, QString const& portable_root)
{
    QFile marker{ marker_path };
    if (!marker.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return {};
    }

    QTextStream stream{ &marker };
    while (!stream.atEnd())
    {
        auto const line = stream.readLine().trimmed();
        constexpr auto Prefix = "config-dir=";
        if (line.startsWith(QLatin1String{ Prefix }))
        {
            auto const value = line.mid(static_cast<int>(sizeof(Prefix) - 1)).trimmed();
            if (!value.isEmpty())
            {
                return QDir::cleanPath(QDir::isAbsolutePath(value) ? value : QDir{ portable_root }.absoluteFilePath(value));
            }
        }
    }
    return {};
}

bool copyFileIfMissing(QString const& source, QString const& destination)
{
    if (QFileInfo::exists(destination))
    {
        return true;
    }

    auto const parent = QFileInfo{ destination }.absolutePath();
    if (!QDir{}.mkpath(parent))
    {
        qWarning() << "Portable mode: cannot create directory" << parent;
        return false;
    }
    if (!QFile::copy(source, destination))
    {
        qWarning() << "Portable mode: cannot import" << source << "to" << destination;
        return false;
    }
    return true;
}

bool copyDirectoryIfMissing(QString const& source_path, QString const& destination_path)
{
    QDir const source{ source_path };
    if (!source.exists())
    {
        return true;
    }
    if (!QDir{}.mkpath(destination_path))
    {
        qWarning() << "Portable mode: cannot create directory" << destination_path;
        return false;
    }

    bool success = true;
    for (auto const& entry : source.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System))
    {
        if (entry.isSymLink())
        {
            continue;
        }
        auto const target = QDir{ destination_path }.absoluteFilePath(entry.fileName());
        if (entry.isDir())
        {
            success = copyDirectoryIfMissing(entry.absoluteFilePath(), target) && success;
        }
        else if (entry.isFile())
        {
            success = copyFileIfMissing(entry.absoluteFilePath(), target) && success;
        }
    }
    return success;
}
} // namespace

namespace trqt::portable
{
Config resolve(QString const& executable_path, QString const& explicit_config_dir, bool portable_flag)
{
    auto executable = QFileInfo{ executable_path }.absoluteFilePath();
#ifdef Q_OS_WIN
    // argv[0] may be a bare command name when launched through PATH.
    auto module_path = std::vector<wchar_t>(32768);
    auto const module_length = GetModuleFileNameW(nullptr, module_path.data(), static_cast<DWORD>(module_path.size()));
    if (module_length > 0 && module_length < module_path.size())
    {
        executable = QString::fromWCharArray(module_path.data(), static_cast<qsizetype>(module_length));
    }
#endif
    auto const portable_root = QFileInfo{ executable }.absolutePath();
    auto const marker_path = QDir{ portable_root }.absoluteFilePath(QStringLiteral("portable.dat"));
    auto const enabled = portable_flag || QFileInfo{ marker_path }.isFile();

    if (!enabled)
    {
        return { explicit_config_dir, false };
    }

    auto directory = !explicit_config_dir.isNull() && !explicit_config_dir.isEmpty() ?
        QDir::cleanPath(QDir{ explicit_config_dir }.absolutePath()) : markerConfigDirectory(marker_path, portable_root);
    if (directory.isEmpty())
    {
        directory = portable_root;
    }

    auto const official = officialConfigDirectory();
    if (!official.isEmpty() && QDir::cleanPath(official).compare(QDir::cleanPath(directory), Qt::CaseInsensitive) != 0)
    {
        importOfficialConfig(official, directory);
    }
    return { directory, true };
}

void importOfficialConfig(QString const& official_directory, QString const& portable_directory)
{
    auto const source = QDir::cleanPath(QFileInfo{ official_directory }.absoluteFilePath());
    auto const destination = QDir::cleanPath(QFileInfo{ portable_directory }.absoluteFilePath());
    if (source.compare(destination, Qt::CaseInsensitive) == 0)
    {
        return;
    }

    if (!QDir{}.mkpath(destination))
    {
        qWarning() << "Portable mode: cannot create destination" << destination;
        return;
    }

    // Import exactly once. If interrupted, the next launch retries missing files.
    auto const completion_marker = QDir{ destination }.absoluteFilePath(QStringLiteral(".portable-import-complete"));
    if (QFileInfo::exists(completion_marker))
    {
        return;
    }

    bool success = true;
    auto const settings = QDir{ source }.absoluteFilePath(QStringLiteral("settings.json"));
    if (QFileInfo{ settings }.isFile())
    {
        success = copyFileIfMissing(settings, QDir{ destination }.absoluteFilePath(QStringLiteral("settings.json"))) && success;
    }
    for (auto const& name : { QStringLiteral("resume"), QStringLiteral("torrents") })
    {
        success = copyDirectoryIfMissing(QDir{ source }.absoluteFilePath(name), QDir{ destination }.absoluteFilePath(name)) && success;
    }

    if (success)
    {
        QFile marker{ completion_marker };
        if (!marker.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            qWarning() << "Portable mode: unable to write import completion marker" << completion_marker;
        }
        else
        {
            marker.write("Imported from official Transmission config.\n");
        }
    }
}
} // namespace trqt::portable
