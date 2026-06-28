#include <gtest/gtest.h>
#include "lob/queue_model.hpp"
#include "lob/reconstructor.hpp"
#include "lob/feature_engine.hpp"

using namespace lob;

static BookMessage bid(MessageType type, Price px, Qty qty, uint64_t ts = 1) {
    return BookMessage{.seq = ts, .ts_us = ts, .type = type, .px = px, .qty = qty, .side = Side::Buy};
}

static BookMessage ask(MessageType type, Price px, Qty qty, uint64_t ts = 1) {
    return BookMessage{.seq = ts, .ts_us = ts, .type = type, .px = px, .qty = qty, .side = Side::Sell};
}

TEST(QueueModelTest, DepthAhead) {
    Reconstructor rec;
    FeatureEngine fe;

    rec.apply(bid(MessageType::Add, 1'005'000, 500, 1));
    rec.apply(bid(MessageType::Add, 1'004'000, 300, 1));
    rec.apply(ask(MessageType::Add, 1'006'000, 200, 1));

    fe.update(bid(MessageType::Add, 1'005'000, 500, 1), rec);
    fe.update(bid(MessageType::Add, 1'004'000, 300, 1), rec);
    fe.update(ask(MessageType::Add, 1'006'000, 200, 1), rec);

    // Hypothetical buy order at 100.50 — depth ahead = 500
    auto f = QueueModel::extract_features(rec, fe, 1'005'000, 10, Side::Buy);
    EXPECT_EQ(f.depth_ahead, 500);

    // Hypothetical sell order at 100.60 — depth ahead = 200
    f = QueueModel::extract_features(rec, fe, 1'006'000, 10, Side::Sell);
    EXPECT_EQ(f.depth_ahead, 200);

    // No volume at this price
    f = QueueModel::extract_features(rec, fe, 1'007'000, 10, Side::Sell);
    EXPECT_EQ(f.depth_ahead, 0);
}

TEST(QueueModelTest, LevelImbalance) {
    Reconstructor rec;
    FeatureEngine fe;

    rec.apply(bid(MessageType::Add, 1'005'000, 300, 1));
    rec.apply(ask(MessageType::Add, 1'005'000, 100, 1));  // same price both sides

    fe.update(bid(MessageType::Add, 1'005'000, 300, 1), rec);
    fe.update(ask(MessageType::Add, 1'005'000, 100, 1), rec);

    auto f = QueueModel::extract_features(rec, fe, 1'005'000, 10, Side::Buy);
    // level_imbalance = bid / (bid + ask) = 300 / 400 = 0.75
    EXPECT_DOUBLE_EQ(f.level_imbalance, 0.75);
}

TEST(QueueModelTest, MarketFeaturesCopied) {
    Reconstructor rec;
    FeatureEngine fe;

    rec.apply(bid(MessageType::Add, 1'005'000, 200, 1));
    rec.apply(ask(MessageType::Add, 1'007'000, 100, 1));

    fe.update(bid(MessageType::Add, 1'005'000, 200, 1), rec);
    fe.update(ask(MessageType::Add, 1'007'000, 100, 1), rec);

    auto f = QueueModel::extract_features(rec, fe, 1'006'000, 10, Side::Buy);

    EXPECT_DOUBLE_EQ(f.midprice, 100.60);
    EXPECT_NEAR(f.spread, 0.20, 0.0001);
    EXPECT_DOUBLE_EQ(f.top_imbalance, (200.0 - 100.0) / (200.0 + 100.0));
}

TEST(QueueModelTest, OrderFields) {
    Reconstructor rec;
    FeatureEngine fe;

    rec.apply(bid(MessageType::Add, 1'005'000, 100, 1));
    rec.apply(ask(MessageType::Add, 1'006'000, 100, 1));
    fe.update(bid(MessageType::Add, 1'005'000, 100, 1), rec);
    fe.update(ask(MessageType::Add, 1'006'000, 100, 1), rec);

    auto f = QueueModel::extract_features(rec, fe, 1'005'500, 15, Side::Sell);
    EXPECT_EQ(f.order_price, 1'005'500);
    EXPECT_EQ(f.order_qty, 15);
    EXPECT_EQ(f.order_side, Side::Sell);
}
