#include <gtest/gtest.h>
#include "lob/binance_parser.hpp"

using namespace lob;

// ── depthUpdate tests ───────────────────────────────────────────────────────

TEST(BinanceParserTest, ParseDepthUpdateBidsAndAsks) {
    std::string json = R"({
        "e": "depthUpdate",
        "E": 1719000000123,
        "s": "BTCUSDT",
        "U": 100,
        "u": 103,
        "b": [
            ["100.50", "2.300"],
            ["100.49", "3.000"]
        ],
        "a": [
            ["100.51", "1.100"],
            ["100.52", "5.000"]
        ]
    })";

    auto result = BinanceParser::parse_depth_update(json);

    EXPECT_EQ(result.first_update_id, 100);
    EXPECT_EQ(result.final_update_id, 103);
    EXPECT_EQ(result.messages.size(), 4);

    // Bids first (b array before a array)
    EXPECT_EQ(result.messages[0].side, Side::Buy);
    EXPECT_EQ(result.messages[0].px, to_price(100.50));
    EXPECT_EQ(result.messages[0].qty, 2);
    EXPECT_EQ(result.messages[0].type, MessageType::Modify);

    EXPECT_EQ(result.messages[1].side, Side::Buy);
    EXPECT_EQ(result.messages[1].px, to_price(100.49));
    EXPECT_EQ(result.messages[1].qty, 3);

    // Asks
    EXPECT_EQ(result.messages[2].side, Side::Sell);
    EXPECT_EQ(result.messages[2].px, to_price(100.51));
    EXPECT_EQ(result.messages[2].qty, 1);

    EXPECT_EQ(result.messages[3].side, Side::Sell);
    EXPECT_EQ(result.messages[3].px, to_price(100.52));
    EXPECT_EQ(result.messages[3].qty, 5);
}

TEST(BinanceParserTest, ParseDepthUpdateZeroQtyBecomesDelete) {
    std::string json = R"({
        "e": "depthUpdate",
        "E": 1719000000123,
        "s": "BTCUSDT",
        "U": 200,
        "u": 200,
        "b": [["100.50", "0.00000000"]],
        "a": []
    })";

    auto result = BinanceParser::parse_depth_update(json);
    ASSERT_EQ(result.messages.size(), 1);
    EXPECT_EQ(result.messages[0].type, MessageType::Delete);
    EXPECT_EQ(result.messages[0].px, to_price(100.50));
    EXPECT_EQ(result.messages[0].qty, 0);
}

TEST(BinanceParserTest, ParseDepthUpdateTimestampConversion) {
    std::string json = R"({
        "e": "depthUpdate",
        "E": 1719000000001,
        "s": "BTCUSDT",
        "U": 1,
        "u": 1,
        "b": [["100.00", "1.0"]],
        "a": []
    })";

    auto result = BinanceParser::parse_depth_update(json);
    // 1719000000001 ms → 1719000000001000 μs
    EXPECT_EQ(result.messages[0].ts_us, 1'719'000'000'001'000ULL);
}

TEST(BinanceParserTest, ParseDepthUpdateSeqFromU) {
    std::string json = R"({
        "e": "depthUpdate",
        "E": 0,
        "s": "BTCUSDT",
        "U": 42,
        "u": 99,
        "b": [["100.00", "1.0"]],
        "a": [["101.00", "2.0"]]
    })";

    auto result = BinanceParser::parse_depth_update(json);
    for (const auto& msg : result.messages) {
        EXPECT_EQ(msg.seq, 99);  // uses 'u' as stable seq
    }
}

TEST(BinanceParserTest, ParseDepthUpdateEmptyArrays) {
    std::string json = R"({
        "e": "depthUpdate",
        "E": 0,
        "s": "BTCUSDT",
        "U": 1,
        "u": 1,
        "b": [],
        "a": []
    })";

    auto result = BinanceParser::parse_depth_update(json);
    EXPECT_TRUE(result.messages.empty());
}

// ── Snapshot tests ──────────────────────────────────────────────────────────

TEST(BinanceParserTest, ParseSnapshot) {
    std::string json = R"({
        "lastUpdateId": 1027024,
        "bids": [
            ["100.50", "2.300"],
            ["100.49", "3.000"]
        ],
        "asks": [
            ["100.51", "1.100"],
            ["100.52", "5.000"]
        ]
    })";

    auto msgs = BinanceParser::parse_snapshot(json);

    ASSERT_EQ(msgs.size(), 4);
    for (const auto& m : msgs) {
        EXPECT_EQ(m.type, MessageType::Snapshot);
    }

    EXPECT_EQ(msgs[0].side, Side::Buy);
    EXPECT_EQ(msgs[0].px, to_price(100.50));
    EXPECT_EQ(msgs[0].qty, 2);

    EXPECT_EQ(msgs[2].side, Side::Sell);
    EXPECT_EQ(msgs[2].px, to_price(100.51));
    EXPECT_EQ(msgs[2].qty, 1);
}

TEST(BinanceParserTest, ParseSnapshotSkipsZeroQty) {
    std::string json = R"({
        "lastUpdateId": 1,
        "bids": [["100.00", "0.00000000"]],
        "asks": []
    })";

    auto msgs = BinanceParser::parse_snapshot(json);
    EXPECT_TRUE(msgs.empty());
}

// ── Edge cases ──────────────────────────────────────────────────────────────

TEST(BinanceParserTest, LargePriceValue) {
    std::string json = R"({
        "e": "depthUpdate",
        "E": 0,
        "s": "BTCUSDT",
        "U": 1,
        "u": 1,
        "b": [["99999.9999", "1.0"]],
        "a": []
    })";

    auto result = BinanceParser::parse_depth_update(json);
    ASSERT_EQ(result.messages.size(), 1);
    EXPECT_EQ(result.messages[0].px, to_price(99999.9999));
}

TEST(BinanceParserTest, PrecisionPreservation) {
    std::string json = R"({
        "e": "depthUpdate",
        "E": 0,
        "s": "BTCUSDT",
        "U": 1,
        "u": 1,
        "b": [["0.0001", "0.0001"]],
        "a": []
    })";

    auto result = BinanceParser::parse_depth_update(json);
    ASSERT_EQ(result.messages.size(), 1);
    EXPECT_EQ(result.messages[0].px, 1);   // 0.0001 * 10000 = 1
    EXPECT_EQ(result.messages[0].qty, 0);  // 0.0001 rounds to 0 for int64_t qty
}
