// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <algorithm> // std::count_if, std::min_element
#include <array>
#include <cstddef> // size_t
#include <cstdint>
#include <ctime> // time_t
#include <functional>
#include <utility>

// A fixed-size cache that erases least-recently-used items to make room for new ones.
template<typename Key, typename Val, std::size_t N>
class tr_lru_cache
{
public:
    // If a nonzero TTL is set, entries that haven't been used recently
    // enough are treated as missing and dropped on the next cache upkeep.
    void set_ttl(time_t ttl_secs) noexcept
    {
        ttl_secs_ = ttl_secs;
    }

    [[nodiscard]] time_t ttl() const noexcept
    {
        return ttl_secs_;
    }

    // Drops any entries that were last used more than `ttl_secs` ago,
    // and advances the cache's clock to `now`.
    // `now` should be monotonically increasing, e.g. tr_time().
    void expire_stale_entries(time_t now)
    {
        use_time_ = now;

        if (ttl_secs_ == 0)
        {
            return;
        }

        for (auto& entry : entries_)
        {
            if (entry.sequence_ != InvalidSeq && is_expired(entry, now))
            {
                erase(entry);
            }
        }
    }

    [[nodiscard]] constexpr Val* get(Key const& key) noexcept
    {
        if (auto const found = find(key); found != nullptr)
        {
            if (is_expired(*found, use_time_))
            {
                erase(*found);
                return nullptr;
            }

            found->sequence_ = next_sequence_++;
            found->last_used_ = use_time_;
            return &found->val_;
        }

        return nullptr;
    }

    [[nodiscard]] constexpr bool contains(Key const& key) const noexcept
    {
        return find(key) != nullptr;
    }

    Val& add(Key&& key)
    {
        auto& entry = get_free_slot();
        entry.key_ = std::move(key);
        entry.sequence_ = next_sequence_++;
        entry.last_used_ = use_time_;

        key = {};
        return entry.val_;
    }

    // Drops the least-recently-used entry, e.g. to honor a shrunken size limit.
    void erase_least_recently_used()
    {
        if (empty())
        {
            return;
        }

        erase(*std::min_element(
            std::begin(entries_),
            std::end(entries_),
            [](auto const& a, auto const& b) { return a.sequence_ < b.sequence_; }));
    }

    [[nodiscard]] size_t size() const noexcept
    {
        return static_cast<size_t>(std::count_if(std::begin(entries_), std::end(entries_), [](auto const& entry)
                                                     { return entry.sequence_ != InvalidSeq; }));
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return size() == 0U;
    }

    void erase(Key const& key)
    {
        if (auto* const found = find(key); found != nullptr)
        {
            this->erase(*found);
        }
    }

    void erase_if(std::function<bool(Key const&, Val const&)> const& test)
    {
        for (auto& entry : entries_)
        {
            if (entry.sequence_ != InvalidSeq && test(entry.key_, entry.val_))
            {
                erase(entry);
            }
        }
    }

    void clear()
    {
        for (auto& entry : entries_)
        {
            erase(entry);
        }
    }

private:
    struct Entry
    {
        Key key_ = {};
        Val val_ = {};
        uint64_t sequence_ = InvalidSeq;
        time_t last_used_ = {};
    };

    // Expired entries are only reported as missing & erased lazily,
    // so this is a no-op when there's no TTL set.
    [[nodiscard]] constexpr bool is_expired(Entry const& entry, time_t now) const noexcept
    {
        return ttl_secs_ != 0 && entry.last_used_ + ttl_secs_ < now;
    }

    void erase(Entry& entry) const
    {
        entry.key_ = {};
        entry.val_ = {};
        entry.sequence_ = InvalidSeq;
    }

    [[nodiscard]] constexpr Entry* find(Key const& key) noexcept
    {
        for (auto& entry : entries_)
        {
            if (entry.sequence_ != InvalidSeq && entry.key_ == key)
            {
                return &entry;
            }
        }

        return nullptr;
    }

    [[nodiscard]] constexpr Entry const* find(Key const& key) const noexcept
    {
        for (auto const& entry : entries_)
        {
            if (entry.sequence_ != InvalidSeq && entry.key_ == key)
            {
                return &entry;
            }
        }

        return nullptr;
    }

    Entry& get_free_slot()
    {
        auto const iter = std::min_element(
            std::begin(entries_),
            std::end(entries_),
            [](auto const& a, auto const& b) { return a.sequence_ < b.sequence_; });
        erase(*iter);
        return *iter;
    }

    std::array<Entry, N> entries_;
    uint64_t next_sequence_ = 1U;
    static uint64_t constexpr InvalidSeq = 0U;

    // the 'now' that get() & add() stamp entries with; kept fresh by
    // expire_stale_entries(). While it is still 0 (epoch) -- i.e. before any
    // caller has ticked the clock -- entries added under a nonzero TTL are
    // stamped as already-expired and re-stamped on their next get().
    time_t use_time_ = {};

    // how long entries may go unused before they're dropped; 0 means no expiry
    time_t ttl_secs_ = {};
};
