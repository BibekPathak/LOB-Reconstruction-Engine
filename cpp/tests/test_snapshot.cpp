#include <gtest/gtest.h>
#include "lob/snapshot.hpp"
#include "lob/reconstructor.hpp"
#include "lob/feature_engine.hpp"

using namespace lob;

static BookMessage bid(MessageType type, Price px, Qty qty, uint64_t ts = 1) {
    return BookMessage{.seq = ts, .ts_us = ts, .type = type, .px = px, .qty = qty, .side = Side::Buy};
}

static BookMessage ask(MessageType type, Price px, Qty qty, uint64_t ts = 1) {
    return BookMessage{.seq = ts, .ts_us = ts, .type = type, .px = px, .qty = qty, .side = Side::Sell};
}

// ── Time trigger ────────────────────────────────────────────────────────────

TEST(SnapshotEngineTest, TimeTrigger) {
    Reconstructor rec;
    FeatureEngine fe;
    SnapshotEngine se;

    // Message at t=0
    rec.apply(bid(MessageType::Add, 1'005'000, 100, 0));
    fe.update(bid(MessageType::Add, 1'005'000, 100, 0), rec);
    se.check(rec, fe, 0);
    EXPECT_EQ(se.data().size(), 1);  // first message always triggers

    // Messages within 100ms window — no new snapshot
    rec.apply(ask(MessageType::Add, 1'007'000, 100, 50'000));
    fe.update(ask(MessageType::Add, 1'007'000, 100, 50'000), rec);
    se.check(rec, fe, 50'000);
    EXPECT_EQ(se.data().size(), 1);

    // Message past 100ms — triggers
    rec.apply(bid(MessageType::Modify, 1'005'000, 200, 150'000));
    fe.update(bid(MessageType::Modify, 1'005'000, 200, 150'000), rec);
    se.check(rec, fe, 150'000);
    EXPECT_EQ(se.data().size(), 2);
}

// ── Count trigger ───────────────────────────────────────────────────────────

TEST(SnapshotEngineTest, CountTrigger) {
    Reconstructor rec;
    FeatureEngine fe;
    SnapshotEngine se;

    // First message triggers
    rec.apply(bid(MessageType::Add, 1'005'000, 100, 0));
    fe.update(bid(MessageType::Add, 1'005'000, 100, 0), rec);
    se.check(rec, fe, 0);
    EXPECT_EQ(se.data().size(), 1);

    // Send COUNT_THRESHOLD messages (resets to 0 after trigger, so we need 1000)
    for (uint64_t i = 1; i <= SnapshotEngine::COUNT_THRESHOLD; ++i) {
        auto m = bid(MessageType::Modify, 1'005'000, static_cast<Qty>(100 + i), 1);
        rec.apply(m);
        fe.update(m, rec);
        se.check(rec, fe, 1);
    }
    // Count threshold hit at i == COUNT_THRESHOLD (since msg_count_ resets to 0 after trigger)
    EXPECT_EQ(se.data().size(), 2);
}

// ── Snapshot content ────────────────────────────────────────────────────────

TEST(SnapshotEngineTest, SnapshotFields) {
    Reconstructor rec;
    FeatureEngine fe;
    SnapshotEngine se;

    rec.apply(bid(MessageType::Add, 1'005'000, 200, 100));
    fe.update(bid(MessageType::Add, 1'005'000, 200, 100), rec);
    se.check(rec, fe, 100);

    rec.apply(ask(MessageType::Add, 1'007'000, 100, 100));
    fe.update(ask(MessageType::Add, 1'007'000, 100, 100), rec);
    se.check(rec, fe, 100);

    ASSERT_EQ(se.data().size(), 1);  // second check is same ts → no trigger
    // Force a snapshot by advancing time
    se.check(rec, fe, 200'000);

    ASSERT_EQ(se.data().size(), 2);
    const auto& ms = se.data().back();

    EXPECT_EQ(ms.best_bid, 1'005'000);
    EXPECT_EQ(ms.best_bid_qty, 200);
    EXPECT_EQ(ms.best_ask, 1'007'000);
    EXPECT_EQ(ms.best_ask_qty, 100);
    EXPECT_DOUBLE_EQ(ms.midprice, 100.60);
    EXPECT_NEAR(ms.spread, 0.20, 0.0001);
    EXPECT_NEAR(ms.microprice, (100.50 * 100 + 100.70 * 200) / 300.0, 0.0001);
    EXPECT_DOUBLE_EQ(ms.queue_imbalance, (200.0 - 100.0) / (200.0 + 100.0));
}

// ── Reset ───────────────────────────────────────────────────────────────────

TEST(SnapshotEngineTest, Reset) {
    Reconstructor rec;
    FeatureEngine fe;
    SnapshotEngine se;

    se.check(rec, fe, 0);
    EXPECT_EQ(se.data().size(), 1);

    se.reset();
    EXPECT_TRUE(se.data().empty());
}
