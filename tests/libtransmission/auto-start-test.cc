// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <memory>

#include <libtransmission/torrent-ctor.h>
#include <libtransmission/torrent.h>
#include <libtransmission/transmission.h>

#include "test-fixtures.h"

using AutoStartTest = tr::test::SessionTest;

namespace
{
auto constexpr TestTorrent = "ubuntu-18.04.6-desktop-amd64.iso.torrent"sv;
}

// Test that adding a torrent with start_added_torrents=true auto-starts it
TEST_F(AutoStartTest, AutoStartWhenEnabled)
{
    // Enable auto-start
    tr_sessionSet(session_, TR_KEY_start_added_torrents, true);

    auto ctor = tr_ctor{ session_ };
    ctor.set_metainfo_from_file(TestTorrent);

    tr_torrent* tor = tr_torrentNew(&ctor, nullptr);
    ASSERT_NE(tor, nullptr);

    // Torrent should be started (downloading or queued)
    EXPECT_TRUE(tor->is_running()) << "Torrent should be running when start_added_torrents=true";
    EXPECT_NE(tor->activity(), TR_STATUS_STOPPED) << "Torrent should not be stopped";

    tr_torrentRemove(tor, false, nullptr);
}

// Test that adding a torrent with start_added_torrents=false keeps it paused
TEST_F(AutoStartTest, AutoStartWhenDisabled)
{
    // Disable auto-start
    tr_sessionSet(session_, TR_KEY_start_added_torrents, false);

    auto ctor = tr_ctor{ session_ };
    ctor.set_metainfo_from_file(TestTorrent);

    tr_torrent* tor = tr_torrentNew(&ctor, nullptr);
    ASSERT_NE(tor, nullptr);

    // Torrent should be paused
    EXPECT_FALSE(tor->is_running()) << "Torrent should be paused when start_added_torrents=false";
    EXPECT_EQ(tor->activity(), TR_STATUS_STOPPED) << "Torrent should be stopped";

    tr_torrentRemove(tor, false, nullptr);
}

// Test that explicit paused=true overrides start_added_torrents
TEST_F(AutoStartTest, ExplicitPausedOverridesAutoStart)
{
    // Enable auto-start but explicitly pause
    tr_sessionSet(session_, TR_KEY_start_added_torrents, true);

    auto ctor = tr_ctor{ session_ };
    ctor.set_metainfo_from_file(TestTorrent);
    ctor.set_paused(TR_FORCE, true);

    tr_torrent* tor = tr_torrentNew(&ctor, nullptr);
    ASSERT_NE(tor, nullptr);

    // Torrent should be paused despite auto-start being enabled
    EXPECT_FALSE(tor->is_running()) << "Explicit paused=true should override auto-start";
    EXPECT_EQ(tor->activity(), TR_STATUS_STOPPED) << "Torrent should be stopped";

    tr_torrentRemove(tor, false, nullptr);
}

// Test that explicit paused=false works with auto-start disabled
TEST_F(AutoStartTest, ExplicitStartOverridesAutoStop)
{
    // Disable auto-start but explicitly start
    tr_sessionSet(session_, TR_KEY_start_added_torrents, false);

    auto ctor = tr_ctor{ session_ };
    ctor.set_metainfo_from_file(TestTorrent);
    ctor.set_paused(TR_FORCE, false);

    tr_torrent* tor = tr_torrentNew(&ctor, nullptr);
    ASSERT_NE(tor, nullptr);

    // Torrent should be started despite auto-start being disabled
    EXPECT_TRUE(tor->is_running()) << "Explicit paused=false should override auto-stop";
    EXPECT_NE(tor->activity(), TR_STATUS_STOPPED) << "Torrent should not be stopped";

    tr_torrentRemove(tor, false, nullptr);
}

// Test torrent start/stop via RPC-like calls
TEST_F(AutoStartTest, ManualStartStop)
{
    tr_sessionSet(session_, TR_KEY_start_added_torrents, false);

    auto ctor = tr_ctor{ session_ };
    ctor.set_metainfo_from_file(TestTorrent);

    tr_torrent* tor = tr_torrentNew(&ctor, nullptr);
    ASSERT_NE(tor, nullptr);

    // Initially stopped
    EXPECT_EQ(tor->activity(), TR_STATUS_STOPPED);

    // Start torrent
    tr_torrentStart(tor);
    EXPECT_TRUE(tor->is_running());
    EXPECT_NE(tor->activity(), TR_STATUS_STOPPED);

    // Stop torrent
    tor->stop_soon();
    // Need to process session thread
    tr_session* s = session_;
    s->run_in_session_thread([](){});
    // Wait a bit
    std::this_thread::sleep_for(10ms);
    EXPECT_EQ(tor->activity(), TR_STATUS_STOPPED);

    tr_torrentRemove(tor, false, nullptr);
}

// Test torrent start_now bypasses queue
TEST_F(AutoStartTest, StartNowBypassesQueue)
{
    // Fill download queue with 5 torrents (default queue size)
    tr_sessionSet(session_, TR_KEY_download_queue_enabled, true);
    tr_sessionSet(session_, TR_KEY_download_queue_size, 5);
    tr_sessionSet(session_, TR_KEY_start_added_torrents, true);

    std::array<tr_torrent*, 5> torrents;
    for (int i = 0; i < 5; ++i)
    {
        auto ctor = tr_ctor{ session_ };
        ctor.set_metainfo_from_file(TestTorrent);
        torrents[i] = tr_torrentNew(&ctor, nullptr);
        ASSERT_NE(torrents[i], nullptr);
    }

    // All 5 should be downloading
    for (int i = 0; i < 5; ++i)
    {
        EXPECT_EQ(torrents[i]->activity(), TR_STATUS_DOWNLOAD) << "Torrent " << i << " should be downloading";
    }

    // Add 6th torrent - should be queued
    auto ctor6 = tr_ctor{ session_ };
    ctor6.set_metainfo_from_file(TestTorrent);
    tr_torrent* tor6 = tr_torrentNew(&ctor6, nullptr);
    ASSERT_NE(tor6, nullptr);
    EXPECT_EQ(tor6->activity(), TR_STATUS_DOWNLOAD_WAIT) << "6th torrent should be queued";

    // StartNow should bypass queue
    tr_torrentStartNow(tor6);
    EXPECT_NE(tor6->activity(), TR_STATUS_DOWNLOAD_WAIT) << "StartNow should bypass queue";

    // Cleanup
    for (auto* t : torrents) tr_torrentRemove(t, false, nullptr);
    tr_torrentRemove(tor6, false, nullptr);
}

// Test resume file preserves start_when_stable
TEST_F(AutoStartTest, ResumeFilePreservesStartState)
{
    tr_sessionSet(session_, TR_KEY_start_added_torrents, true);

    auto ctor = tr_ctor{ session_ };
    ctor.set_metainfo_from_file(TestTorrent);

    tr_torrent* tor = tr_torrentNew(&ctor, nullptr);
    ASSERT_NE(tor, nullptr);

    int const tor_id = tor->id();
    EXPECT_TRUE(tor->is_running());

    // Save resume data
    tor->saveResumeData();

    // Remove torrent
    tr_torrentRemove(tor, false, nullptr);

    // Re-add torrent (should load resume)
    auto ctor2 = tr_ctor{ session_ };
    ctor2.set_metainfo_from_file(TestTorrent);
    tr_torrent* tor2 = tr_torrentNew(&ctor2, nullptr);
    ASSERT_NE(tor2, nullptr);

    // Should resume in same state (running)
    EXPECT_EQ(tor2->id(), tor_id);
    EXPECT_TRUE(tor2->is_running()) << "Resume should preserve running state";

    tr_torrentRemove(tor2, false, nullptr);
}