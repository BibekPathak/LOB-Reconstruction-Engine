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
    EXPECT_DOUBLE_EQ(fe.current().bid_slope, 0.0);
    EXPECT_DOUBLE_EQ(fe.current().volatility, 0.0);
    EXPECT_DOUBLE_EQ(fe.current().trade_intensity, 0.0);
}

// ── v2: Book Slope ───────────────────────────────────────────────────────────

TEST(FeatureEngineTest, BidSlopeUniform) {
    Reconstructor rec;
    FeatureEngine fe;

    // 5 bid levels at uniform qty 1000 → slope ≈ 0
    for (int i = 0; i < 5; ++i) {
        Price px = 1'005'000 - i * 100;  // 100.50, 100.49, ...
        auto m = bid(MessageType::Add, px, 1000, i * 1000);
        rec.apply(m);
        fe.update(m, rec);
    }
    rec.apply(ask(MessageType::Add, 1'007'000, 1000));
    fe.update(ask(MessageType::Add, 1'007'000, 1000), rec);

    // Uniform volumes → near-zero slope
    EXPECT_NEAR(fe.current().bid_slope, 0.0, 0.05);
}

TEST(FeatureEngineTest, BidSlopeDeclining) {
    Reconstructor rec;
    FeatureEngine fe;

    // 5 bid levels with decreasing volume → positive slope
    // (volume drops as we go deeper = negative log-volume slope = aggression)
    Qty volumes[] = {2000, 1500, 1000, 500, 100};
    for (int i = 0; i < 5; ++i) {
        Price px = 1'005'000 - i * 100;
        auto m = bid(MessageType::Add, px, volumes[i], i * 1000);
        rec.apply(m);
        fe.update(m, rec);
    }
    rec.apply(ask(MessageType::Add, 1'007'000, 1000));
    fe.update(ask(MessageType::Add, 1'007'000, 1000), rec);

    // Declining volumes → negative slope (log-volume decreases with depth)
    EXPECT_LT(fe.current().bid_slope, -0.1);
}

// ── v2: Volatility ───────────────────────────────────────────────────────────

TEST(FeatureEngineTest, VolatilityZero) {
    Reconstructor rec;
    FeatureEngine fe;

    // Same midprice repeatedly → zero volatility
    for (int i = 0; i < 15; ++i) {
        auto b = bid(MessageType::Add, 1'005'000, 100, i * 1000);
        rec.apply(b);
        fe.update(b, rec);
        auto a = ask(MessageType::Add, 1'007'000, 100, i * 1000 + 1);
        rec.apply(a);
        fe.update(a, rec);
    }

    EXPECT_NEAR(fe.current().volatility, 0.0, 1e-6);
}

TEST(FeatureEngineTest, VolatilityNonZero) {
    Reconstructor rec;
    FeatureEngine fe;

    // Add a fixed bid at 100.50 so midprice is computed on every update
    uint64_t bid_oid = 1000;
    rec.apply(BookMessage{.seq = 0, .ts_us = 0, .type = MessageType::Add, .order_id = bid_oid, .px = 1'005'000, .qty = 100, .side = Side::Buy});
    fe.update(BookMessage{.seq = 0, .ts_us = 0, .type = MessageType::Add, .order_id = bid_oid, .px = 1'005'000, .qty = 100, .side = Side::Buy}, rec);

    // Delete+Add ask at oscillating prices → real top-of-book change each iteration
    for (int i = 0; i < 12; ++i) {
        uint64_t ask_oid = 2000 + i;
        Price ask_px = 1'007'000 + (i % 2 == 0 ? 0 : 200);  // 100.70, 100.72, 100.70, ...
        uint64_t ts = 100 + i * 1000;

        // Delete previous ask level (skip first iteration)
        if (i > 0) {
            uint64_t prev_oid = 2000 + (i - 1);
            rec.apply(BookMessage{.seq = ts - 1, .ts_us = ts - 1, .type = MessageType::Delete, .order_id = prev_oid, .side = Side::Sell});
            fe.update(BookMessage{.seq = ts - 1, .ts_us = ts - 1, .type = MessageType::Delete, .order_id = prev_oid, .side = Side::Sell}, rec);
        }

        rec.apply(BookMessage{.seq = ts, .ts_us = ts, .type = MessageType::Add, .order_id = ask_oid, .px = ask_px, .qty = 100, .side = Side::Sell});
        fe.update(BookMessage{.seq = ts, .ts_us = ts, .type = MessageType::Add, .order_id = ask_oid, .px = ask_px, .qty = 100, .side = Side::Sell}, rec);
    }

    EXPECT_GT(fe.current().volatility, 0.0);
}

// ── v2: Trade Intensity / Buy Ratio ─────────────────────────────────────────

static BookMessage trade_msg(uint64_t order_id, Side resting_side, Price px,
                             Qty qty, uint64_t ts) {
    return BookMessage{.seq = ts, .ts_us = ts,
                       .type = MessageType::Trade,
                       .order_id = order_id,
                       .px = px, .qty = qty,
                       .side = resting_side};
}

static BookMessage add_with_ref(MessageType type, Price px, Qty qty,
                                 uint64_t order_id, uint64_t ts = 1) {
    return BookMessage{.seq = ts, .ts_us = ts,
                       .type = type,
                       .order_id = order_id,
                       .px = px, .qty = qty, .side = Side::Buy};
}

static BookMessage ask_with_ref(MessageType type, Price px, Qty qty,
                                 uint64_t order_id, uint64_t ts = 1) {
    return BookMessage{.seq = ts, .ts_us = ts,
                       .type = type,
                       .order_id = order_id,
                       .px = px, .qty = qty, .side = Side::Sell};
}

TEST(FeatureEngineTest, TradeIntensity) {
    Reconstructor rec;
    FeatureEngine fe;

    // Add a bid order with order_id=1
    rec.apply(add_with_ref(MessageType::Add, 1'005'000, 1000, 1, 1000));
    fe.update(add_with_ref(MessageType::Add, 1'005'000, 1000, 1, 1000), rec);
    rec.apply(ask_with_ref(MessageType::Add, 1'007'000, 1000, 2, 1001));
    fe.update(ask_with_ref(MessageType::Add, 1'007'000, 1000, 2, 1001), rec);

    // 3 sell trades hitting the bid (order_id=1, resting side=Buy)
    Price trade_px = 1'005'000;
    for (int i = 0; i < 3; ++i) {
        auto t = trade_msg(1, Side::Buy, trade_px, 100, 2000 + i * 1000);
        rec.apply(t);
        fe.update(t, rec);
    }

    EXPECT_NEAR(fe.current().trade_intensity, 3.0, 0.1);
}

TEST(FeatureEngineTest, BuyRatio) {
    Reconstructor rec;
    FeatureEngine fe;

    rec.apply(add_with_ref(MessageType::Add, 1'005'000, 1000, 1, 1000));
    fe.update(add_with_ref(MessageType::Add, 1'005'000, 1000, 1, 1000), rec);
    rec.apply(ask_with_ref(MessageType::Add, 1'007'000, 1000, 2, 1001));
    fe.update(ask_with_ref(MessageType::Add, 1'007'000, 1000, 2, 1001), rec);

    // 2 trades against bid (order_id=1, resting=Buy, trade_px=1.005.000)
    // sell aggressor hits bid → NOT a buy trade → buy_trade_events not pushed
    for (int i = 0; i < 2; ++i) {
        auto t = trade_msg(1, Side::Buy, 1'005'000, 100, 2000 + i * 1000);
        rec.apply(t);
        fe.update(t, rec);
    }

    // 1 trade against ask (order_id=2, resting=Sell, trade_px=1.007.000)
    // buy aggressor lifts ask → buy trade → buy_trade_events incremented
    {
        auto t = trade_msg(2, Side::Sell, 1'007'000, 100, 4000);
        rec.apply(t);
        fe.update(t, rec);
    }

    // 3 total trades, 1 buy-initiated → ratio = 1/3
    EXPECT_NEAR(fe.current().buy_ratio, 1.0 / 3.0, 0.01);
}
