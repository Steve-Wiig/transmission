// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cerrno>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <unordered_map>
#include <vector>

#include "libtransmission/local-data.h"

#include "libtransmission/crypto-utils.h"
#include "libtransmission/error.h"
#include "libtransmission/inout.h"
#include "libtransmission/open-files.h"
#include "libtransmission/torrent.h"
#include "libtransmission/torrents.h"
#include "libtransmission/transmission.h"

namespace tr {
namespace
{

struct HashResult
{
    tr_error_code_t error = 0;
    std::optional<tr_sha1_digest_t> hash;
};

[[nodiscard]] tr_error make_error(tr_error_code_t err)
{
    auto error = tr_error{};
    if (err != 0)
    {
        error.set_from_errno(err);
    }

    return error;
}

[[nodiscard]] HashResult recalculate_hash(
    LocalData::Backend& backend,
    tr_torrent_id_t const id,
    tr_block_info const block_info,
    tr_piece_index_t const piece)
{
TR_ASSERT(piece < block_info.piece_count());

    auto sha = tr_sha1{};
    auto buffer = LocalData::BlockData{};

    auto const [begin_byte, end_byte] = block_info.byte_span_for_piece(piece);
    auto const [begin_block, end_block] = block_info.block_span_for_piece(piece);
    [[maybe_unused]] auto n_bytes_checked = size_t{};
    for (auto block = begin_block; block < end_block; ++block)
    {
        auto const byte_span = block_info.byte_span_for_block(block);

        buffer.clear();
        if (auto const err = backend.read(id, byte_span, buffer); err != 0)
        {
            return { .error = err, .hash = {} };
        }

        auto* begin = std::data(buffer);
        auto* end = begin + byte_span.size();

        if (block == begin_block)
        {
            begin += (begin_byte - byte_span.begin);
        }
        if (block + 1U == end_block)
        {
            end -= (byte_span.end - end_byte);
        }

        sha.add(begin, end - begin);
        n_bytes_checked += (end - begin);
    }

    TR_ASSERT(block_info.piece_size(piece) == n_bytes_checked);
    return { .error = 0, .hash = sha.finish() };
}
class DefaultBackend final : public LocalData::Backend
{
public:
    DefaultBackend(tr_torrents const& torrents, tr_open_files& open_files)
        : open_files_{ open_files }
        , torrents_{ torrents }
    {

    }

    [[nodiscard]] tr_error_code_t read(tr_torrent_id_t const id, tr_byte_span_t const byte_span, LocalData::BlockData& setme)
        override
    {
        if (!byte_span.is_valid())
        {
            return TR_ERROR_EINVAL;
        }

        auto const len = byte_span.size();
        if (len > tr_block_info::BlockSize)
        {
            return TR_ERROR_EINVAL;
        }
        auto const span_size = static_cast<size_t>(len);

        auto const* const tor = torrents_.get(id);
        if (tor == nullptr)
        {
            return TR_ERROR_EINVAL;
        }

        auto const loc = tor->block_info().byte_loc(byte_span.begin);
        setme.resize(span_size);
        return tr_ioRead(*tor, open_files_, loc, std::span{ std::data(setme), span_size });
    }

    [[nodiscard]] tr_error_code_t test_piece(
        tr_torrent_id_t const id,
        tr_piece_index_t const piece,
        tr_sha1_digest_t& setme_hash) override
    {
        auto const* const tor = torrents_.get(id);
        if (tor == nullptr || piece >= tor->piece_count())
        {
            return TR_ERROR_EINVAL;
        }

        auto const result = recalculate_hash(*this, id, tor->block_info(), piece);
        if (!result.hash)
        {
            return result.error != 0 ? result.error : EIO;
        }

        setme_hash = *result.hash;
        return 0;
    }

    [[nodiscard]] tr_error_code_t write(
        tr_torrent_id_t const id,
        tr_byte_span_t const byte_span,
        LocalData::BlockData const& data) override
    {
        if (!byte_span.is_valid())
        {
            return TR_ERROR_EINVAL;
        }

        auto const len = byte_span.size();
        if (len > std::size(data))
        {
            return TR_ERROR_EINVAL;
        }
        auto const span_size = static_cast<size_t>(len);

        auto* const tor = torrents_.get(id);
        if (tor == nullptr)
        {
            return TR_ERROR_EINVAL;
        }

        auto const loc = tor->block_info().byte_loc(byte_span.begin);

        // Buffer the write to reduce SMB syscall count.
        // On SMB, each pwrite() is a network round-trip. Accumulating writes
        // in system RAM and flushing in larger chunks dramatically reduces
        // the number of syscalls, keeping the session thread responsive.
        std::lock_guard<std::mutex> lock{write_mutex_};

        auto& buf = write_buffers_[id];
        if (buf.empty())
        {
            buf.reserve(WriteFlushThreshold);
        }

        auto const buf_start = buf.size();
        buf.resize(buf_start + span_size);
        std::memcpy(std::data(buf) + buf_start, std::data(data), span_size);

        // Flush when buffer reaches threshold
        if (buf.size() >= WriteFlushThreshold)
        {
            return flush_buffer(*tor, loc, std::move(buf));
        }

        return 0; // buffered, not yet flushed
    }

    void close_all() override
    {
        std::lock_guard<std::mutex> lock{write_mutex_};
        flush_all_buffers();
        open_files_.close_all();
    }

    void close_torrent(tr_torrent_id_t const tor_id) override
    {
        std::lock_guard<std::mutex> lock{write_mutex_};
        flush_torrent_buffer(tor_id);
        open_files_.close_torrent(tor_id);
    }

    void close_file(tr_torrent_id_t const tor_id, tr_file_index_t const file_num) override
    {
        std::lock_guard<std::mutex> lock{write_mutex_};
        open_files_.close_file(tor_id, file_num);
    }

    [[nodiscard]] tr_error_code_t move(tr_torrent_id_t const id, std::string_view old_parent, std::string_view parent, std::string_view parent_name) override
    {
        return TR_ERROR_EINVAL;
    }

    [[nodiscard]] tr_error_code_t remove(tr_torrent_id_t const id, tr_torrent_remove_func remove_func) override
    {
        return TR_ERROR_EINVAL;
    }

    void rename(tr_torrent_id_t const id, std::string_view oldpath, std::string_view newname, tr_torrent_rename_done_func callback) override
    {
        static_cast<void>(callback);
    }

private:
    static constexpr size_t WriteFlushThreshold = 256U * 1024U; // 256KB flush threshold

    mutable std::mutex write_mutex_;

    tr_error_code_t flush_buffer(tr_torrent& tor, tr_block_info::Location const& loc, std::vector<uint8_t>&& buf)
    {
        // NOTE: Caller must hold write_mutex_. This is an internal helper.

        if (buf.empty())
        {
            return 0;
        }

        auto error = tr_error{};
        tr_sys_file_write_at(
            get_fd_for_write(tor, error),
            std::data(buf),
            std::size(buf),
            loc.byte,
            nullptr,
            &error);

        buf.clear();
        return error.code();
    }

    void flush_all_buffers()
    {
        // NOTE: Caller must hold write_mutex_. This is an internal helper.

        for (auto& [id, buf] : write_buffers_)
        {
            if (!buf.empty())
            {
                auto* const tor = torrents_.get(id);
                if (tor != nullptr)
                {
                    auto const loc = tor->block_info().byte_loc(0);
                    flush_buffer(*tor, loc, std::move(buf));
                }
            }
        }
    }

    void flush_torrent_buffer(tr_torrent_id_t const tor_id)
    {
        // NOTE: Caller must hold write_mutex_. This is an internal helper.

        auto it = write_buffers_.find(tor_id);
        if (it != write_buffers_.end() && !it->second.empty())
        {
            auto* const tor = torrents_.get(tor_id);
            if (tor != nullptr)
            {
                auto const loc = tor->block_info().byte_loc(0);
                flush_buffer(*tor, loc, std::move(it->second));
            }
        }
    }

    tr_sys_file_t get_fd_for_write(tr_torrent& tor, tr_error& error)
    {
        // Get or open the file for writing
        auto const fd = open_files_.get(tor.id(), 0, true);
        if (!fd || error)
        {
            error.set_from_errno(EIO);
            return TR_BAD_SYS_FILE;
        }
        return *fd;
    }

    tr_open_files& open_files_;
    tr_torrents const& torrents_;
    /// Buffered writes may be lost on crash (up to WriteFlushThreshold
    /// per torrent). This is acceptable for BitTorrent since missing
    /// pieces are re-downloaded. Do NOT use this pattern for data where
    /// loss is unacceptable.
    std::unordered_map<tr_torrent_id_t, std::vector<uint8_t>> write_buffers_;
};

} // namespace
LocalData::LocalData(tr_torrents const& torrents, tr_open_files& open_files, [[maybe_unused]] size_t worker_count)
    : backend_{ std::make_unique<DefaultBackend>(torrents, open_files) }
{

}

LocalData::LocalData(std::unique_ptr<Backend> backend, [[maybe_unused]] size_t worker_count)
    : backend_{ std::move(backend) }
{

}

LocalData::~LocalData() = default;

// NOLINTNEXTLINE(performance-unnecessary-value-param): keep callback by value so a later async backend can take ownership.
void LocalData::read(tr_torrent_id_t const id, tr_byte_span_t const byte_span, OnRead on_read)
{
    auto data = std::make_unique<BlockData>();
    auto const err = backend_->read(id, byte_span, *data);
    if (err != 0)
    {
        data.reset();
    }

    if (on_read)
    {
        std::move(on_read)(id, byte_span, make_error(err), std::move(data));
    }
}

// NOLINTNEXTLINE(performance-unnecessary-value-param): keep callback by value so a later async backend can take ownership.
void LocalData::test_piece(tr_torrent_id_t const id, tr_piece_index_t const piece, OnTest on_test)
{
    auto hash = tr_sha1_digest_t{};
    auto const err = backend_->test_piece(id, piece, hash);

    if (on_test)
    {
        std::move(on_test)(id, piece, make_error(err), err == 0 ? std::optional<tr_sha1_digest_t>{ hash } : std::nullopt);
    }
}

void LocalData::write(
    tr_torrent_id_t const id,
    tr_byte_span_t const byte_span,
    std::unique_ptr<BlockData> data,
    OnWrite on_write) // NOLINT(performance-unnecessary-value-param)
{
    auto err = tr_error_code_t{ TR_ERROR_EINVAL };
    if (data != nullptr)
    {
        err = backend_->write(id, byte_span, *data);
    }

    if (on_write)
    {
        std::move(on_write)(id, byte_span, make_error(err));
    }
}

void LocalData::close_torrent(tr_torrent_id_t const tor_id)
{
    backend_->close_torrent(tor_id);
}

void LocalData::close_file(tr_torrent_id_t const tor_id, tr_file_index_t const file_num)
{
    backend_->close_file(tor_id, file_num);
}

void LocalData::close_all()
{
    backend_->close_all();
}

void LocalData::move(
    tr_torrent_id_t const id,
    std::string_view const old_parent,
    std::string_view const parent,
    std::string_view const parent_name,
    OnMove on_move) // NOLINT(performance-unnecessary-value-param)
{
    auto const err = backend_->move(id, old_parent, parent, parent_name);

    if (on_move)
    {
        std::move(on_move)(id, make_error(err));
    }
}

void LocalData::remove(tr_torrent_id_t const id, tr_torrent_remove_func remove_func)
{
    static_cast<void>(backend_->remove(id, std::move(remove_func)));
}

void LocalData::rename(
    tr_torrent_id_t const id,
    std::string_view const oldpath,
    std::string_view const newname,
    tr_torrent_rename_done_func callback)
{
    backend_->rename(id, oldpath, newname, std::move(callback));
}

void LocalData::shutdown()
{
}

uint64_t LocalData::enqueued_write_bytes()
{
    return 0U;
}

} // namespace tr