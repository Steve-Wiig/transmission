// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/folder.

#include <chrono>
#include <vector>

#include <gtest/gtest.h>

#include <libtransmission/completion.h>
#include <libtransmission/peer-mgr-wishlist.h>
#include <libtransmission/block-info.h>
#include <libtransmission/torrent.h>

using namespace std::chrono;

class PerformanceTest : public ::testing::Test
{
protected:
    tr_block_info block_info_{ 1000, 16384 }; // 1000 pieces, 16KiB blocks
};

TEST(PerformanceTest, Completion_ComputeHasValid_Benchmark)
{
    tr_completion completion(nullptr, &block_info_);

    // Set half the blocks as present
    for (tr_block_index_t i = 0; i < block_info_.block_count() / 2; ++i)
    {
        completion.add_block(i);
    }

    // Warm up
    for (int i = 0; i < 10; ++i)
    {
        completion.has_valid();
    }

    // Benchmark
    auto const start = high_resolution_clock::now();
    constexpr int kIterations = 10000;
    for (int i = 0; i < kIterations; ++i)
    {
        auto val = completion.has_valid();
        (void)val;
    }
    auto const end = high_resolution_clock::now();

    auto const duration = duration_cast<microseconds>(end - start);
    auto const per_call = static_cast<double>(duration.count()) / kIterations;

    std::cout << "Completion::has_valid (cached): " << per_call << " us per call\n";

    // Should be very fast due to caching
    EXPECT_LT(per_call, 1.0);
}

TEST(PerformanceTest, Completion_ComputeHasValid_Uncached_Benchmark)
{
    tr_completion completion(nullptr, &block_info_);

    // Set half the blocks as present
    for (tr_block_index_t i = 0; i < block_info_.block_count() / 2; ++i)
    {
        completion.add_block(i);
    }

    // Invalidate cache
    completion.has_valid_; // This is private, can't directly invalidate
    // Instead, add a block to invalidate
    completion.add_block(block_info_.block_count() / 2);

    // Benchmark (will recompute each time due to cache invalidation)
    auto const start = high_resolution_clock::now();
    constexpr int kIterations = 1000;
    for (int i = 0; i < kIterations; ++i)
    {
        completion.add_block(i % block_info_.block_count());
        auto val = completion.has_valid();
        (void)val;
    }
    auto const end = high_resolution_clock::now();

    auto const duration = duration_cast<microseconds>(end - start);
    auto const per_call = static_cast<double>(duration.count()) / kIterations;

    std::cout << "Completion::has_valid (uncached): " << per_call << " us per call\n";

    // Should still be reasonably fast
    EXPECT_LT(per_call, 100.0);
}

TEST(PerformanceTest, Completion_ComputeSizeWhenDone_Benchmark)
{
    tr_completion completion(nullptr, &block_info_);

    // Set some blocks as present
    for (tr_block_index_t i = 0; i < block_info_.block_count() / 4; ++i)
    {
        completion.add_block(i);
    }

    // Warm up
    for (int i = 0; i < 10; ++i)
    {
        completion.size_when_done();
    }

    // Benchmark
    auto const start = high_resolution_clock::now();
    constexpr int kIterations = 10000;
    for (int i = 0; i < kIterations; ++i)
    {
        auto val = completion.size_when_done();
        (void)val;
    }
    auto const end = high_resolution_clock::now();

    auto const duration = duration_cast<microseconds>(end - start);
    auto const per_call = static_cast<double>(duration.count()) / kIterations;

    std::cout << "Completion::size_when_done (cached): " << per_call << " us per call\n";

    EXPECT_LT(per_call, 1.0);
}

TEST(PerformanceTest, Wishlist_Next_Benchmark)
{
    // Create a simple mock mediator
    class MockMediator : public tr_wishlist::Mediator
    {
    public:
        MockMediator(tr_block_info const& bi) : block_info_(bi) {}

        [[nodiscard]] bool client_has_block(tr_block_index_t block) const override
        {
            return block < 100; // First 100 blocks are present
        }

        [[nodiscard]] bool client_has_piece(tr_piece_index_t piece) const override
        {
            return piece < 10;
        }

        [[nodiscard]] bool client_wants_piece(tr_piece_index_t piece) const override
        {
            return piece < 500;
        }

        [[nodiscard]] bool is_sequential_download() const override
        {
            return false;
        }

        [[nodiscard]] tr_piece_index_t sequential_download_from_piece() const override
        {
            return 0;
        }

        [[nodiscard]] int priority(tr_piece_index_t) const override
        {
            return 1;
        }

        [[nodiscard]] int count_piece_replication(tr_piece_index_t piece) const override
        {
            return piece < 100 ? 5 : 1;
        }

        [[nodiscard]] tr_piece_index_t piece_count() const override
        {
            return 1000;
        }

        [[nodiscard]] tr_block_span_t block_span(tr_piece_index_t piece) const override
        {
            auto const begin = piece * 16; // 16 blocks per piece
            return { begin, begin + 16 };
        }

    private:
        tr_block_info const& block_info_;
    };

    tr_block_info block_info(1000, 16384);
    MockMediator mediator(block_info);
    tr_wishlist::Wishlist wishlist(mediator);

    // Warm up
    for (int i = 0; i < 10; ++i)
    {
        wishlist.next(16, [](tr_piece_index_t) { return true; });
    }

    // Benchmark
    auto const start = high_resolution_clock::now();
    constexpr int kIterations = 1000;
    for (int i = 0; i < kIterations; ++i)
    {
        auto spans = wishlist.next(16, [](tr_piece_index_t) { return true; });
        (void)spans;
    }
    auto const end = high_resolution_clock::now();

    auto const duration = duration_cast<microseconds>(end - start);
    auto const per_call = static_cast<double>(duration.count()) / kIterations;

    std::cout << "Wishlist::next: " << per_call << " us per call\n";

    EXPECT_LT(per_call, 500.0);
}

TEST(PerformanceTest, BlockInfo_Benchmark)
{
    tr_block_info block_info(10000, 16384); // Large torrent

    // Warm up
    for (int i = 0; i < 10; ++i)
    {
        block_info.byte_loc(1000000);
    }

    // Benchmark
    auto const start = high_resolution_clock::now();
    constexpr int kIterations = 100000;
    for (int i = 0; i < kIterations; ++i)
    {
        auto loc = block_info.byte_loc(i * 1000);
        (void)loc;
    }
    auto const end = high_resolution_clock::now();

    auto const duration = duration_cast<microseconds>(end - start);
    auto const per_call = static_cast<double>(duration.count()) / kIterations;

    std::cout << "BlockInfo::byte_loc: " << per_call << " us per call\n";

    EXPECT_LT(per_call, 1.0);
}