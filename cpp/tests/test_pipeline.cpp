#include <gtest/gtest.h>
#include "lob/binance_parser.hpp"
#include "lob/normalizer.hpp"
#include "lob/reconstructor.hpp"

using namespace lob;

// ── Full pipeline: snapshot + depth updates ─────────────────────────────────

TEST(PipelineTest, SnapshotThenDepthUpdates) {
    // Step 1: Snapshot with 3 bids and 2 asks
    std::string snap_json = R"({
        "lastUpdateId": 1000,
        "bids": [
            ["100.50", "2.300"],
            ["100.49", "3.000"],
            ["100.48", "1.500"]
        ],
        "asks": [
            ["100.51", "1.100"],
            ["100.52", "5.000"]
        ]
    })";

    auto snap_msgs = BinanceParser::parse_snapshot(snap_json);

    Normalizer norm;
    Reconstructor rec;

    for (const auto& m : norm.process_snapshot(snap_msgs)) {
        rec.apply(m);
    }

    auto s = rec.snapshot();
    EXPECT_EQ(s.bid.price, to_price(100.50));
    EXPECT_EQ(s.bid.volume, 2);
    EXPECT_EQ(s.ask.price, to_price(100.51));
    EXPECT_EQ(s.ask.volume, 1);
    EXPECT_EQ(rec.bids().size(), 3);
    EXPECT_EQ(rec.asks().size(), 2);

    // Step 2: depthUpdate — adds new best bid, modifies existing ask, deletes a bid
    std::string update1 = R"({
        "e": "depthUpdate",
        "E": 1719000001000,
        "s": "BTCUSDT",
        "U": 1001,
        "u": 1001,
        "b": [
            ["100.51", "1.000"],
            ["100.49", "0.00000000"]
        ],
        "a": [
            ["100.51", "2.000"]
        ]
    })";

    auto result1 = BinanceParser::parse_depth_update(update1);
    auto msgs1 = norm.process(result1);
    for (const auto& m : msgs1) {
        rec.apply(m);
    }

    // New best bid at 100.51 (was 100.50), qty 1
    // 100.49 deleted
    // Best ask at 100.51 now vol 2 (was 1.1)
    s = rec.snapshot();
    EXPECT_EQ(s.bid.price, to_price(100.51));
    EXPECT_EQ(s.bid.volume, 1);
    EXPECT_EQ(s.ask.price, to_price(100.51));
    EXPECT_EQ(s.ask.volume, 2);
    EXPECT_EQ(rec.bids().size(), 3);  // 100.51, 100.50, 100.48
    EXPECT_EQ(rec.asks().size(), 2);  // 100.51, 100.52

    // Step 3: depthUpdate — removes best bid, adds new ask level
    std::string update2 = R"({
        "e": "depthUpdate",
        "E": 1719000002000,
        "s": "BTCUSDT",
        "U": 1002,
        "u": 1002,
        "b": [
            ["100.51", "0.00000000"]
        ],
        "a": [
            ["100.50", "10.000"]
        ]
    })";

    auto result2 = BinanceParser::parse_depth_update(update2);
    auto msgs2 = norm.process(result2);
    for (const auto& m : msgs2) {
        rec.apply(m);
    }

    // Best bid falls back to 100.50 (vol 2.3)
    // Best ask stays at 100.51 (vol 2) — 100.50 is a bid level not ask
    // Actually wait: the new ask at 100.50 is BELOW the current best ask 100.51
    // So the new best ask becomes 100.50 with vol 10
    s = rec.snapshot();
    EXPECT_EQ(s.bid.price, to_price(100.50));
    EXPECT_EQ(s.bid.volume, 2);
    EXPECT_EQ(s.ask.price, to_price(100.50));  // new best ask
    EXPECT_EQ(s.ask.volume, 10);
}

// ── Normalizer upgrades Modify → Add on first sighting ──────────────────────

TEST(PipelineTest, NormalizerUpgradesModifyToAdd) {
    Normalizer norm;

    // Process a depthUpdate with a new price level
    std::string json = R"({
        "e": "depthUpdate",
        "E": 1000,
        "s": "BTCUSDT",
        "U": 1,
        "u": 1,
        "b": [["100.00", "5.0"]],
        "a": []
    })";

    auto result = BinanceParser::parse_depth_update(json);
    auto msgs = norm.process(result);

    ASSERT_EQ(msgs.size(), 1);
    EXPECT_EQ(msgs[0].type, MessageType::Add);  // upgraded from Modify

    // Second sighting stays Modify
    result = BinanceParser::parse_depth_update(json);
    msgs = norm.process(result);
    ASSERT_EQ(msgs.size(), 1);
    EXPECT_EQ(msgs[0].type, MessageType::Modify);
}

// ── Normalizer resets on snapshot ───────────────────────────────────────────

TEST(PipelineTest, NormalizerResetOnSnapshot) {
    Normalizer norm;

    // First, see a price level
    std::string json = R"({
        "e": "depthUpdate",
        "E": 1000,
        "s": "BTCUSDT",
        "U": 1,
        "u": 1,
        "b": [["100.00", "5.0"]],
        "a": []
    })";

    auto msgs = norm.process(BinanceParser::parse_depth_update(json));
    EXPECT_EQ(msgs[0].type, MessageType::Add);

    // Process a snapshot (clears seen_ state — snapshot doesn't include 100.00)
    std::string snap = R"({"lastUpdateId":100,"bids":[["101.00","5.0"]],"asks":[]})";
    norm.process_snapshot(BinanceParser::parse_snapshot(snap));

    // Now the same price should be Add again
    msgs = norm.process(BinanceParser::parse_depth_update(json));
    EXPECT_EQ(msgs[0].type, MessageType::Add);
}

// ── Deleted price can be re-added ───────────────────────────────────────────

TEST(PipelineTest, DeleteThenReaddPromotesToAdd) {
    Normalizer norm;

    // Add a level
    std::string add_json = R"({
        "e": "depthUpdate","E":0,"s":"BTCUSDT","U":1,"u":1,
        "b": [["100.00","5.0"]], "a": []
    })";
    auto msgs = norm.process(BinanceParser::parse_depth_update(add_json));
    EXPECT_EQ(msgs[0].type, MessageType::Add);

    // Delete it
    std::string del_json = R"({
        "e": "depthUpdate","E":0,"s":"BTCUSDT","U":2,"u":2,
        "b": [["100.00","0.0"]], "a": []
    })";
    norm.process(BinanceParser::parse_depth_update(del_json));

    // Re-add — should be Add again
    msgs = norm.process(BinanceParser::parse_depth_update(add_json));
    EXPECT_EQ(msgs[0].type, MessageType::Add);
}
