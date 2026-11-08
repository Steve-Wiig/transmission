// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "PortableRemap.h"

#include "PortableImporter.h"

#include <libtransmission/quark.h>
#include <libtransmission/variant.h>
#include <libtransmission/variant-serde.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <vector>

namespace tr::portable
{
namespace
{

// Helper to remap a path string using drive_to_unc
QString remapPath(const QString& path)
{
    if (path.isEmpty())
        return path;

    std::string utf8 = path.toUtf8().constData();
    std::string remapped = drive_to_unc(utf8);
    return QString::fromUtf8(remapped.c_str());
}

// Helper to check if a path is a drive-letter path (C:\...)
bool isDriveLetterPath(const QString& path)
{
    return path.size() >= 2 && path[1] == ':' && path[0].isLetter();
}

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

// Resume file keys that contain paths needing remapping
static constexpr tr_quark resume_path_keys[] = {
    TR_KEY_destination,
    TR_KEY_incomplete_dir,
};

bool migrateSettings(const QString& settingsPath)
{
    QFileInfo fi(settingsPath);
    if (!fi.exists() || !fi.isFile())
    {
        return false;
    }

    // Parse settings.json using JSON serde
    auto serde = tr_variant_serde::json();
    auto opt_var = serde.parse_file(settingsPath.toUtf8().constData());
    if (!opt_var)
    {
        return false;
    }

    auto var = std::move(*opt_var);
    auto* map = var.map();
    if (!map)
    {
        return false;
    }

    bool modified = false;

    // Remap path values
    for (auto const& key_info : settings_path_keys)
    {
        if (auto const* val = map->find_if<tr_variant::String>(key_info.key); val != nullptr)
        {
            QString path = QString::fromUtf8(val->value().c_str());
            QString remapped = remapPath(path);
            if (remapped != path)
            {
                map->insert_or_assign(key_info.key, remapped.toUtf8().constData());
                modified = true;
            }
        }
    }

    // Security sanitization: disable dangerous automation
    for (auto const& key : settings_disable_keys)
    {
        if (auto const* val = map->find_if<tr_variant::Bool>(key); val != nullptr)
        {
            if (val->value())
            {
                map->insert_or_assign(key, false);
                modified = true;
            }
        }
        else
        {
            // Key doesn't exist - add it as disabled
            map->insert_or_assign(key, false);
            modified = true;
        }
    }

    // Also ensure watch-dir is empty if watch-dir-enabled is disabled
    if (auto const* val = map->find_if<tr_variant::String>(TR_KEY_watch_dir); val != nullptr)
    {
        if (!val->value().empty())
        {
            map->insert_or_assign(TR_KEY_watch_dir, "");
            modified = true;
        }
    }

    if (!modified)
    {
        return true; // Nothing to change
    }

    // Write back using QSaveFile for atomic writes
    QSaveFile saveFile(settingsPath);
    if (!saveFile.open(QIODevice::WriteOnly))
    {
        return false;
    }

    // Serialize to JSON string
    auto const json_str = serde.to_string(var);
    if (saveFile.write(json_str.c_str(), json_str.size()) == -1)
    {
        saveFile.cancelWriting();
        return false;
    }

    if (!saveFile.commit())
    {
        return false;
    }

    return true;
}

bool migrateResumeFiles(const QString& resumeDir)
{
    QDir dir(resumeDir);
    if (!dir.exists())
    {
        return false;
    }

    QFileInfoList resumeFiles = dir.entryInfoList(QStringList() << "*.resume", QDir::Files);
    if (resumeFiles.isEmpty())
    {
        return true; // No resume files to migrate
    }

    auto serde = tr_variant_serde::benc();
    bool any_modified = false;

    for (auto const& fileInfo : resumeFiles)
    {
        QString filePath = fileInfo.absoluteFilePath();

        // Parse .resume file using bencode serde
        auto opt_var = serde.parse_file(filePath.toUtf8().constData());
        if (!opt_var)
        {
            // Corrupt resume file - skip but don't fail entirely
            continue;
        }

        auto var = std::move(*opt_var);
        auto* map = var.map();
        if (!map)
        {
            continue;
        }

        bool modified = false;

        // Remap path values in resume file
        for (auto const& key : resume_path_keys)
        {
            if (auto const* val = map->find_if<tr_variant::String>(key); val != nullptr)
            {
                QString path = QString::fromUtf8(val->value().c_str());
                QString remapped = remapPath(path);
                if (remapped != path)
                {
                    map->insert_or_assign(key, remapped.toUtf8().constData());
                    modified = true;
                }
            }
        }

        if (!modified)
        {
            continue;
        }

        any_modified = true;

        // Write back using QSaveFile for atomic writes
        QSaveFile saveFile(filePath);
        if (!saveFile.open(QIODevice::WriteOnly))
        {
            continue;
        }

        // Serialize to bencode string
        auto const benc_str = serde.to_string(var);
        if (saveFile.write(benc_str.c_str(), benc_str.size()) == -1)
        {
            saveFile.cancelWriting();
            continue;
        }

        if (!saveFile.commit())
        {
            continue;
        }
    }

    return any_modified;
}

} // namespace tr::portable