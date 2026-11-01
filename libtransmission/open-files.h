// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstddef> // for size_t
#include <cstdint> // for uintX_t
#include <ctime> // for time_t
#include <optional>
#include <string_view>
#include <utility>

#include "libtransmission/file.h" // tr_sys_file_t
#include "libtransmission/lru-cache.h"
#include "libtransmission/types.h"

// A pool of open files that are cached while reading / writing torrents' data
class tr_open_files
{
public:
    [[nodiscard]] std::optional<tr_sys_file_t> get(tr_torrent_id_t tor_id, tr_file_index_t file_num, bool writable);

    [[nodiscard]] std::optional<tr_sys_file_t> get(
        tr_torrent_id_t tor_id,
        tr_file_index_t file_num,
        bool writable,
        std::string_view filename,
        tr_file_preallocation allocation,
        uint64_t file_size);

    void close_all();
    void close_torrent(tr_torrent_id_t tor_id);
    void close_file(tr_torrent_id_t tor_id, tr_file_index_t file_num);

    // Number of files kept open simultaneously.
    // A smaller pool means more open()/close() calls; on high-latency
    // filesystems (SMB/NFS/CIFS) those round-trips can stall the IO thread.
    void set_max_open_files(size_t max_open_files);
    [[nodiscard]] size_t max_open_files() const noexcept;

    // How long an unused cached fd stays open before being closed.
    // 0 = never expire (LRU eviction only). Longer TTLs keep fds warm
    // between infrequent reads/writes on slow shares at the cost of
    // holding more handles open.
    void set_cache_ttl(time_t ttl_secs) noexcept;
    [[nodiscard]] time_t cache_ttl() const noexcept;

    // Drops cached fds that haven't been used within the TTL window.
    // Call periodically, e.g. from a session timer.
    void expire_stale_entries(time_t now);

private:
    using Key = std::pair<tr_torrent_id_t, tr_file_index_t>;

    [[nodiscard]] static Key make_key(tr_torrent_id_t tor_id, tr_file_index_t file_num) noexcept
    {
        return std::make_pair(tor_id, file_num);
    }

    struct Val
    {
        Val() noexcept = default;
        Val(Val const&) = delete;
        Val& operator=(Val const&) = delete;
        Val(Val&& that) noexcept
        {
            *this = std::move(that);
        }
        Val& operator=(Val&& that) noexcept
        {
            std::swap(this->fd_, that.fd_);
            std::swap(this->writable_, that.writable_);
            return *this;
        }
        ~Val();

        tr_sys_file_t fd_ = TR_BAD_SYS_FILE;
        bool writable_ = false;
    };

    static constexpr size_t MaxOpenFiles = 32U;
    tr_lru_cache<Key, Val, MaxOpenFiles> pool_;
};
