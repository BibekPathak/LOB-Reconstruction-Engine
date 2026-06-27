#include <gtest/gtest.h>
#include "lob/feature_engine.hpp"
#include "lob/reconstructor.hpp"

using namespace lob;

// ── Helpers ─────────────────────────────────────────────────────────────────

static BookMessage bid(MessageType type, Price px, Qty qty, uint64_t ts = 1) {
    return BookMessage{.seq = ts, .ts_us = ts, .type = type, .px = px, .qty = qty, .side = Side::Buy};
}

static BookMessage ask(MessageType type, Price px, Qty qty, uint64_t ts = 1) {
    return BookMessage{.seq = ts, .ts_us = ts, .type = type, .px = px, .qty = qty, .side = Side::Sell};
}

// ── Midprice ────────────────────────────────────────────────────────────────

TEST(FeatureEngineTest, Midprice) {
    Reconstructor rec;
    FeatureEngine fe;

    rec.apply(bid(MessageType::Add, 1'005'000, 100));  // 100.50
    rec.apply(ask(MessageType::Add, 1'007'000, 100));  // 100.70

    fe.update(bid(MessageType::Add, 1'005'000, 100), rec);
    fe.update(ask(MessageType::Add, 1'007'000, 100), rec);

    EXPECT_DOUBLE_EQ(fe.current().midprice, 100.60);
}

// ── Spread ──────────────────────────────────────────────────────────────────

TEST(FeatureEngineTest, Spread) {
    Reconstructor rec;
    FeatureEngine fe;

    rec.apply(ask(MessageType::Add, 1'007'000, 100));  // 100.70
    rec.apply(bid(MessageType::Add, 1'005'000, 100));  // 100.50

    fe.update(ask(MessageType::Add, 1'007'000, 100), rec);
    fe.update(bid(MessageType::Add, 1'005'000, 100), rec);

    // Compute spread in Price space to avoid float accumulation error
    Price spread_px = to_price(to_double(1'007'000)) - to_price(to_double(1'005'000));
    EXPECT_NEAR(fe.current().spread, to_double(spread_px), 0.0001);
}

// ── Microprice ──────────────────────────────────────────────────────────────

TEST(FeatureEngineTest, Microprice) {
    Reconstructor rec;
    FeatureEngine fe;

    // bid 100.50 qty 200, ask 100.70 qty 100
    rec.apply(bid(MessageType::Add, 1'005'000, 200));
    rec.apply(ask(MessageType::Add, 1'007'000, 100));

    fe.update(bid(MessageType::Add, 1'005'000, 200), rec);
    fe.update(ask(MessageType::Add, 1'007'000, 100), rec);

    // MP = (100.50 * 100 + 100.70 * 200) / (200 + 100)
    //    = (10050 + 20140) / 300 = 30190 / 300 = 100.6333...
    double expected = (100.50 * 100 + 100.70 * 200) / 300.0;
    EXPECT_NEAR(fe.current().microprice, expected, 0.0001);
}

TEST(FeatureEngineTest, MicropriceZeroVolume) {
    Reconstructor rec;
    FeatureEngine fe;

    // No levels — microprice should be 0
    rec.apply(bid(MessageType::Add, 1'005'000, 0));
    fe.update(bid(MessageType::Add, 1'005'000, 0), rec);
    EXPECT_DOUBLE_EQ(fe.current().microprice, 0.0);
}

// ── Queue Imbalance ─────────────────────────────────────────────────────────

TEST(FeatureEngineTest, QueueImbalance) {
    Reconstructor rec;
    FeatureEngine fe;

    rec.apply(bid(MessageType::Add, 1'005'000, 300));
    rec.apply(ask(MessageType::Add, 1'007'000, 100));

    fe.update(bid(MessageType::Add, 1'005'000, 300), rec);
    fe.update(ask(MessageType::Add, 1'007'000, 100), rec);

    // QI = (300 - 100) / (300 + 100) = 200 / 400 = 0.5
    EXPECT_DOUBLE_EQ(fe.current().queue_imbalance, 0.5);
}

TEST(FeatureEngineTest, QueueImbalanceEqual) {
    Reconstructor rec;
    FeatureEngine fe;

    rec.apply(bid(MessageType::Add, 1'005'000, 200));
    rec.apply(ask(MessageType::Add, 1'007'000, 200));

    fe.update(bid(MessageType::Add, 1'005'000, 200), rec);
    fe.update(ask(MessageType::Add, 1'007'000, 200), rec);

    EXPECT_DOUBLE_EQ(fe.current().queue_imbalance, 0.0);
}

// ── OFI ─────────────────────────────────────────────────────────────────────

TEST(FeatureEngineTest, OFIBidIncrease) {
    Reconstructor rec;
    FeatureEngine fe;

    // Add bid at 100.50 with qty 100
    rec.apply(bid(MessageType::Add, 1'005'000, 100));
    fe.update(bid(MessageType::Add, 1'005'000, 100), rec);
    EXPECT_EQ(fe.current().ofi, 100);  // OFI += 100 (bid size increased from 0 to 100)

    // Increase bid to 200
    rec.apply(bid(MessageType::Modify, 1'005'000, 200));
    fe.update(bid(MessageType::Modify, 1'005'000, 200), rec);
    EXPECT_EQ(fe.current().ofi, 200);  // OFI += 100 (another 100 increase)
}

TEST(FeatureEngineTest, OFIAskIncrease) {
    Reconstructor rec;
    FeatureEngine fe;

    rec.apply(ask(MessageType::Add, 1'007'000, 100));
    fe.update(ask(MessageType::Add, 1'007'000, 100), rec);
    EXPECT_EQ(fe.current().ofi, -100);  // OFI -= 100 (ask size increased)
}

TEST(FeatureEngineTest, OFIBidDecreaseIgnored) {
    Reconstructor rec;
    FeatureEngine fe;

    rec.apply(bid(MessageType::Add, 1'005'000, 100));
    fe.update(bid(MessageType::Add, 1'005'000, 100), rec);
    EXPECT_EQ(fe.current().ofi, 100);

    // Decrease bid (OFI should not change — only increases count)
    rec.apply(bid(MessageType::Modify, 1'005'000, 50));
    fe.update(bid(MessageType::Modify, 1'005'000, 50), rec);
    EXPECT_EQ(fe.current().ofi, 100);
}

TEST(FeatureEngineTest, OFIDeleteResetsLevel) {
    Reconstructor rec;
    FeatureEngine fe;

    rec.apply(bid(MessageType::Add, 1'005'000, 100));
    fe.update(bid(MessageType::Add, 1'005'000, 100), rec);
    EXPECT_EQ(fe.current().ofi, 100);

    // Delete the level
    rec.apply(bid(MessageType::Delete, 1'005'000, 0));
    fe.update(bid(MessageType::Delete, 1'005'000, 0), rec);
    EXPECT_EQ(fe.current().ofi, 100);  // no change (decrease is not counted)

    // Re-add — should count as increase from 0 to new qty
    rec.apply(bid(MessageType::Add, 1'005'000, 50));
    fe.update(bid(MessageType::Add, 1'005'000, 50), rec);
    EXPECT_EQ(fe.current().ofi, 150);  // OFI + 50
}

// ── Arrival / Cancel rates ──────────────────────────────────────────────────

TEST(FeatureEngineTest, ArrivalRate) {
    Reconstructor rec;
    FeatureEngine fe;

    uint64_t base_ts = 1'000'000;
    for (int i = 0; i < 5; ++i) {
        auto m = bid(MessageType::Add, 1'005'000 + i * 100, 100, base_ts + i * 100'000);
        rec.apply(m);
        fe.update(m, rec);
    }

    // 5 arrivals in ~400μs window → should all be in 1s window → rate ≈ 5
    EXPECT_NEAR(fe.current().arrival_rate, 5.0, 0.1);
}

TEST(FeatureEngineTest, CancelRate) {
    Reconstructor rec;
    FeatureEngine fe;

    uint64_t base_ts = 1'000'000;
    for (int i = 0; i < 3; ++i) {
        // Add first, then delete
        auto add = bid(MessageType::Add, 1'005'000 + i * 100, 100, base_ts + i * 50'000);
        rec.apply(add);
        fe.update(add, rec);

        auto del = bid(MessageType::Delete, 1'005'000 + i * 100, 0, base_ts + i * 50'000 + 10'000);
        rec.apply(del);
        fe.update(del, rec);
    }

    // 3 cancels in ~80μs → all in 1s window → rate ≈ 3
    EXPECT_NEAR(fe.current().cancel_rate, 3.0, 0.1);
}

TEST(FeatureEngineTest, WindowSlides) {
    Reconstructor rec;
    FeatureEngine fe;

    // Old event at t=1μs
    auto old = bid(MessageType::Add, 1'005'000, 100, 1);
    rec.apply(old);
    fe.update(old, rec);
    EXPECT_NEAR(fe.current().arrival_rate, 1.0, 0.1);

    // New event at t=10s — old event is now outside 1s window
    uint64_t now = 10'000'000;
    auto m = bid(MessageType::Add, 1'006'000, 100, now);
    rec.apply(m);
    fe.update(m, rec);
    EXPECT_NEAR(fe.current().arrival_rate, 1.0, 0.1);  // only the new event
}

// ── Reset ───────────────────────────────────────────────────────────────────

TEST(FeatureEngineTest, Reset) {
    Reconstructor rec;
    FeatureEngine fe;

    rec.apply(bid(MessageType::Add, 1'005'000, 100));
    fe.update(bid(MessageType::Add, 1'005'000, 100), rec);
    EXPECT_NE(fe.current().ofi, 0);

    fe.reset();
    EXPECT_DOUBLE_EQ(fe.current().ofi, 0);
    EXPECT_DOUBLE_EQ(fe.current().arrival_rate, 0);
    EXPECT_TRUE(fe.current().midprice == 0.0);
}
