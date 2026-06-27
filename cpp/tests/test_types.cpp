#include <gtest/gtest.h>
#include "lob/types.hpp"

using namespace lob;

// ── Price conversion ────────────────────────────────────────────────────────

TEST(PriceTest, ToPriceRoundsCorrectly) {
    EXPECT_EQ(to_price(100.50), 1'005'000);
    EXPECT_EQ(to_price(0.0001), 1);
    EXPECT_EQ(to_price(0.0), 0);
}

TEST(PriceTest, ToDoubleRoundTrip) {
    double vals[] = {0.0, 0.0001, 100.50, 99999.9999, -50.25};
    for (double v : vals) {
        EXPECT_DOUBLE_EQ(to_double(to_price(v)), v);
    }
}

TEST(PriceTest, ScaleIsTenThousand) {
    EXPECT_EQ(SCALE, 10'000);
}

// ── Side ────────────────────────────────────────────────────────────────────

TEST(SideTest, Opposite) {
    EXPECT_EQ(opposite(Side::Buy), Side::Sell);
    EXPECT_EQ(opposite(Side::Sell), Side::Buy);
}

// ── MessageType ─────────────────────────────────────────────────────────────

TEST(MessageTypeTest, EnumValues) {
    EXPECT_NE(MessageType::Add, MessageType::Delete);
    EXPECT_EQ(static_cast<int>(MessageType::Snapshot), 0);
    EXPECT_EQ(static_cast<int>(MessageType::Execute), 5);
}

// ── BookMessage ─────────────────────────────────────────────────────────────

TEST(BookMessageTest, DefaultConstruction) {
    BookMessage m;
    EXPECT_EQ(m.seq, 0);
    EXPECT_EQ(m.ts_us, 0);
    EXPECT_EQ(m.type, MessageType::Add);
    EXPECT_EQ(m.order_id, 0);
    EXPECT_EQ(m.px, 0);
    EXPECT_EQ(m.qty, 0);
    EXPECT_EQ(m.side, Side::Buy);
}

TEST(BookMessageTest, AggregateInit) {
    auto m = BookMessage{
        .seq      = 42,
        .ts_us    = 1'000'000,
        .type     = MessageType::Delete,
        .order_id = 9001,
        .px       = to_price(100.50),
        .qty      = 10,
        .side     = Side::Sell
    };
    EXPECT_EQ(m.seq, 42);
    EXPECT_EQ(m.ts_us, 1'000'000);
    EXPECT_EQ(m.type, MessageType::Delete);
    EXPECT_EQ(m.order_id, 9001);
    EXPECT_EQ(m.px, to_price(100.50));
    EXPECT_EQ(m.qty, 10);
    EXPECT_EQ(m.side, Side::Sell);
}

TEST(BookMessageTest, Equality) {
    auto a = BookMessage{.seq = 1, .order_id = 100, .px = to_price(10.0), .qty = 5, .side = Side::Buy};
    auto b = a;
    EXPECT_EQ(a, b);
    b.qty = 10;
    EXPECT_NE(a, b);
}
