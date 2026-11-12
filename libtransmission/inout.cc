// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include "libtransmission/block-info.h" // tr_block_info
#include "libtransmission/crypto-utils.h"
#include "libtransmission/error.h"
#include "libtransmission/file.h"
#include "libtransmission/inout.h"
#include "libtransmission/session.h"
#include "libtransmission/string-utils.h"
#include "libtransmission/torrent-files.h"
#include "libtransmission/torrent.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/tr-strbuf.h" // tr_pathbuf
#include "libtransmission/types.h"
#include "libtransmission/utils.h"

using namespace std::literals;

namespace
{

#ifdef _WIN32
// Windows error codes for transient SMB/CIFS network drops.
// Defined as raw integers to avoid #include <windows.h> MinGW64 include-order warnings.
constexpr int kWinErrorNetnameDeleted = 64;
constexpr int kWinErrorNetworkUnreachable = 1231;
constexpr int kWinErrorUnexpNetErr = 59;
constexpr int kWinErrorBadNetpath = 53;
constexpr int kWinErrorSharingViolation = 32;
constexpr int kWinErrorLockViolation = 33;

[[nodiscard]] bool is_stale_network_error(int code) noexcept
{
    return code == kWinErrorNetnameDeleted ||
           code == kWinErrorNetworkUnreachable ||
           code == kWinErrorUnexpNetErr ||
           code == kWinErrorBadNetpath ||
           code == kWinErrorSharingViolation ||
           code == kWinErrorLockViolation;
}
#else
// POSIX error codes for stale NFS/network mounts
[[nodiscard]] bool is_stale_network_error(int code) noexcept
{
    return code == ESTALE || code == ENOLINK || code == ENETDOWN || code == ENETUNREACH;
}
#endif

bool read_entire_buf(tr_sys_file_t const fd, uint64_t file_offset, std::span<uint8_t> buf, tr_error& error)
{
    while (!std::empty(buf))
    {
        auto n_read = uint64_t{};

        if (!tr_sys_file_read_at(fd, std::data(buf), std::size(buf), file_offset, &n_read, &error))
        {
            return false;
        }

        buf = buf.subspan(n_read);
        file_offset += n_read;
    }

    return true;
}

bool write_entire_buf(tr_sys_file_t const fd, uint64_t file_offset, std::span<uint8_t const> buf, tr_error& error)
{
    while (!std::empty(buf))
    {
        auto n_written = uint64_t{};

        if (!tr_sys_file_write_at(fd, std::data(buf), std::size(buf), file_offset, &n_written, &error))
        {
            return false;
        }

        buf = buf.subspan(n_written);
        file_offset += n_written;
    }

    return true;
}

[[nodiscard]] std::optional<tr_sys_file_t> get_fd(
    tr_session& session,
    tr_open_files& open_files,
    tr_torrent const& tor,
    bool const writable,
    tr_file_index_t const file_index,
    tr_error& error)
{
    auto const tor_id = tor.id();

    // is the file already open in the fd pool?
    if (auto const fd = open_files.get(tor_id, file_index, writable); fd)
    {
        return fd;
    }

    // does the file exist?
    auto const file_size = tor.file_size(file_index);
    auto const prealloc = writable && tor.file_is_wanted(file_index) ? session.preallocationMode() :
                                                                       tr_file_preallocation::None;
    if (auto const found = tor.find_file(file_index); found)
    {
        return open_files.get(tor_id, file_index, writable, found->filename(), prealloc, file_size);
    }

    // do we want to create it?
    auto err = ENOENT;
    if (writable)
    {
        auto const base = tor.current_dir();
        auto const suffix = session.isIncompleteFileNamingEnabled() ? tr_torrent_files::PartialFileSuffix : ""sv;
        auto const filename = tr_pathbuf{ base, '/', tor.file_subpath(file_index), suffix };
        if (auto const fd = open_files.get(tor_id, file_index, writable, filename, prealloc, file_size); fd)
        {
            // make a note that we just created a file
            session.add_file_created();
            return fd;
        }

        err = errno;
    }

    error.set(
        err,
        fmt::format(
            fmt::runtime(_("Couldn't get '{path}': {error} ({error_code})")),
            fmt::arg("path", tor.file_subpath(file_index)),
            fmt::arg("error", tr_strerror(err)),
            fmt::arg("error_code", err)));
    return {};
}

void read_bytes(
    tr_session& session,
    tr_open_files& open_files,
    tr_torrent const& tor,
    tr_file_index_t const file_index,
    uint64_t const file_offset,
    std::span<uint8_t> buf,
    tr_error& error)
{
    TR_ASSERT(file_index < tor.file_count());
    auto const file_size = tor.file_size(file_index);
    TR_ASSERT(file_size == 0U || file_offset < file_size);
    TR_ASSERT(file_offset + std::size(buf) <= file_size);
    if (file_size == 0U)
    {
        return;
    }

    auto const fd = get_fd(session, open_files, tor, false, file_index, error);
    if (!fd || error)
    {
        return;
    }

    read_entire_buf(*fd, file_offset, buf, error);

    if (error)
    {
        if (is_stale_network_error(error.code()))
        {
            open_files.close_file(tor.id(), file_index);
            tr_logAddWarnTor(&tor, fmt::format("Stale network handle for file {}, evicting from cache to retry", file_index));
        }

        tr_logAddErrorTor(
            &tor,
            fmt::format(
                fmt::runtime(_("Couldn't read '{path}': {error} ({error_code})")),
                fmt::arg("path", tor.file_subpath(file_index)),
                fmt::arg("error", error.message()),
                fmt::arg("error_code", error.code())));
    }
}

void write_bytes(
    tr_session& session,
    tr_open_files& open_files,
    tr_torrent const& tor,
    tr_file_index_t const file_index,
    uint64_t const file_offset,
    std::span<uint8_t const> buf,
    tr_error& error)
{
    TR_ASSERT(file_index < tor.file_count());
    auto const file_size = tor.file_size(file_index);
    TR_ASSERT(file_size == 0U || file_offset < file_size);
    TR_ASSERT(file_offset + std::size(buf) <= file_size);
    if (file_size == 0U)
    {
        return;
    }

    auto const fd = get_fd(session, open_files, tor, true, file_index, error);
    if (!fd || error)
    {
        return;
    }

    write_entire_buf(*fd, file_offset, buf, error);

    if (error)
    {
        if (is_stale_network_error(error.code()))
        {
            open_files.close_file(tor.id(), file_index);
            tr_logAddWarnTor(&tor, fmt::format("Stale network handle for file {}, evicting from cache to retry", file_index));
        }

        tr_logAddErrorTor(
            &tor,
            fmt::format(
                fmt::runtime(_("Couldn't save '{path}': {error} ({error_code})")),
                fmt::arg("path", tor.file_subpath(file_index)),
                fmt::arg("error", error.message()),
                fmt::arg("error_code", error.code())));
    }
}

std::optional<tr_sha1_digest_t> recalculate_hash(tr_torrent const& tor, tr_piece_index_t const piece)
{
    TR_ASSERT(piece < tor.piece_count());

    auto sha = tr_sha1{};
    auto& open_files = tor.session->openFiles();
    auto const [begin_byte, end_byte] = tor.block_info().byte_span_for_piece(piece);

    // Read the piece's byte span directly in large chunks instead of one
    // 16 KiB block per tr_ioRead call. tr_ioRead splits at file boundaries
    // internally, so no block-straddling math is needed. Hash input is
    // byte-for-byte identical to the old block loop; SMB round-trips drop
    // by up to 64x per piece on WiFi/NAS storage.
    static constexpr size_t ReadSize = 1024U * 1024U; // 1 MiB cap
    auto buffer = std::vector<uint8_t>(static_cast<size_t>(std::min<uint64_t>(ReadSize, end_byte - begin_byte)));
    [[maybe_unused]] auto n_bytes_checked = size_t{};

    for (auto byte = begin_byte; byte < end_byte;)
    {
        auto const n_this_pass = std::min<uint64_t>(std::size(buffer), end_byte - byte);
        auto const contents = std::span{ std::data(buffer), static_cast<size_t>(n_this_pass) };
        if (tr_ioRead(tor, open_files, tor.byte_loc(byte), contents) != 0)
        {
            return {};
        }

        sha.add(std::data(contents), std::size(contents));
        n_bytes_checked += std::size(contents);
        byte += n_this_pass;
    }

    TR_ASSERT(tor.piece_size(piece) == n_bytes_checked);
    return sha.finish();
}

} // namespace

tr_error_code_t tr_ioRead(
    tr_torrent const& tor,
    tr_open_files& open_files,
    tr_block_info::Location const& loc,
    std::span<uint8_t> const setme)
{
    auto error = tr_error{};
    if (loc.piece >= tor.piece_count())
    {
        error.set_from_errno(EINVAL);
        return error.code();
    }

    auto [file_index, file_offset] = tor.file_offset(loc);
    auto& session = *tor.session;
    auto buf = setme;
    while (!std::empty(buf) && !error)
    {
        auto const bytes_this_pass = std::min<uint64_t>(std::size(buf), tor.file_size(file_index) - file_offset);
        read_bytes(session, open_files, tor, file_index, file_offset, buf.first(bytes_this_pass), error);
        buf = buf.subspan(bytes_this_pass);
        ++file_index;
        file_offset = 0U;
    }

    return error.code();
}

tr_error_code_t tr_ioWrite(
    tr_torrent& tor,
    tr_open_files& open_files,
    tr_block_info::Location const& loc,
    std::span<uint8_t const> const writeme)
{
    auto error = tr_error{};
    if (loc.piece >= tor.piece_count())
    {
        error.set_from_errno(EINVAL);
    }
    else
    {
        auto [file_index, file_offset] = tor.file_offset(loc);
        auto& session = *tor.session;
        auto buf = writeme;
        while (!std::empty(buf) && !error)
        {
            auto const bytes_this_pass = std::min<uint64_t>(std::size(buf), tor.file_size(file_index) - file_offset);
            write_bytes(session, open_files, tor, file_index, file_offset, buf.first(bytes_this_pass), error);
            buf = buf.subspan(bytes_this_pass);
            ++file_index;
            file_offset = 0U;
        }
    }

    // if IO failed, set torrent's error if not already set.
    // Defer the stop: tr_torrentStop() runs stop_now() synchronously on the
    // session thread, which destroys the peer objects. When tr_ioWrite() is
    // called from a peer I/O callback (client_got_block), that frees `this`
    // mid-unwind and the caller then touches freed memory. stop_soon() defers
    // the stop to do_idle_work() at the next bandwidth pulse boundary.
    if (error && !tor.error().is_local_error())
    {
        tor.error().set_local_error(error.message());
        tor.stop_soon();
    }

    return error.code();
}

bool tr_ioTestPiece(tr_torrent const& tor, tr_piece_index_t const piece)
{
    auto const hash = recalculate_hash(tor, piece);
    return hash && *hash == tor.piece_hash(piece);
}
