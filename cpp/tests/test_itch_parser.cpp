#include <gtest/gtest.h>
#include "lob/itch_parser.hpp"
#include "lob/reconstructor.hpp"
#include <cstring>
#include <vector>

using namespace lob;

// ── Helpers ─────────────────────────────────────────────────────────────────

// Build an 11-byte ITCH standard header
static void set_header(uint8_t* buf, char msg_type, uint16_t track_no, uint64_t ts_ns) {
    buf[0] = static_cast<uint8_t>(msg_type);
    buf[1] = 0; buf[2] = 0;                                   // stock_locate
    buf[3] = static_cast<uint8_t>(track_no >> 8);
    buf[4] = static_cast<uint8_t>(track_no);
    // 6-byte timestamp in nanoseconds (big-endian uint48)
    buf[5] = static_cast<uint8_t>((ts_ns >> 40) & 0xFF);
    buf[6] = static_cast<uint8_t>((ts_ns >> 32) & 0xFF);
    buf[7] = static_cast<uint8_t>((ts_ns >> 24) & 0xFF);
    buf[8] = static_cast<uint8_t>((ts_ns >> 16) & 0xFF);
    buf[9] = static_cast<uint8_t>((ts_ns >> 8) & 0xFF);
    buf[10] = static_cast<uint8_t>(ts_ns & 0xFF);
}

static void be16(uint8_t* buf, uint16_t val) {
    buf[0] = static_cast<uint8_t>(val >> 8);
    buf[1] = static_cast<uint8_t>(val);
}

static void be32(uint8_t* buf, uint32_t val) {
    buf[0] = static_cast<uint8_t>((val >> 24) & 0xFF);
    buf[1] = static_cast<uint8_t>((val >> 16) & 0xFF);
    buf[2] = static_cast<uint8_t>((val >> 8) & 0xFF);
    buf[3] = static_cast<uint8_t>(val & 0xFF);
}

static void be64(uint8_t* buf, uint64_t val) {
    for (int i = 7; i >= 0; --i) {
        buf[i] = static_cast<uint8_t>(val & 0xFF);
        val >>= 8;
    }
}

// ── 1. Parse Add Order (A) ─────────────────────────────────────────────────

TEST(ItchParserTest, ParseAddOrder) {
    uint8_t buf[36] = {};  // sizeof(ItchAddOrder)
    set_header(buf, 'A', 1, 5'000'000);  // track=1, ts=5ms
    buf[3] = 0; buf[4] = 1;               // tracking_no = 1
    be64(buf + 11, 1001);                 // order_ref = 1001
    buf[19] = 'B';                         // buy
    be32(buf + 20, 500);                   // shares = 500
    std::memcpy(buf + 24, "AAPL    ", 8);  // stock
    be32(buf + 32, 1'500'000);             // price = $150.0000

    ItchParser parser;
    size_t off = 0;
    auto msg = parser.parse_one(buf, sizeof(buf), off);

    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->type, MessageType::Add);
    EXPECT_EQ(msg->order_id, 1001);
    EXPECT_EQ(msg->side, Side::Buy);
    EXPECT_EQ(msg->px, 1'500'000);
    EXPECT_EQ(msg->qty, 500);
    EXPECT_EQ(msg->ts_us, 5'000'000 / 1000);  // ns → μs
    EXPECT_EQ(off, sizeof(buf));
}

// ── 2. Parse Add Order with MPID (F) ────────────────────────────────────────

TEST(ItchParserTest, ParseAddOrderMpid) {
    uint8_t buf[40] = {};  // sizeof(ItchAddOrderMpid)
    set_header(buf, 'F', 2, 10'000'000);
    be64(buf + 11, 2002);
    buf[19] = 'S';
    be32(buf + 20, 1000);
    std::memcpy(buf + 24, "MSFT    ", 8);
    be32(buf + 32, 4'000'000);             // $400.00
    std::memcpy(buf + 36, "MPID", 4);

    ItchParser parser;
    size_t off = 0;
    auto msg = parser.parse_one(buf, sizeof(buf), off);

    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->type, MessageType::Add);
    EXPECT_EQ(msg->order_id, 2002);
    EXPECT_EQ(msg->side, Side::Sell);
    EXPECT_EQ(msg->px, 4'000'000);
    EXPECT_EQ(msg->qty, 1000);
    EXPECT_EQ(off, sizeof(buf));
}

// ── 3. Parse Order Executed (E) ────────────────────────────────────────────

TEST(ItchParserTest, ParseOrderExecuted) {
    uint8_t buf[31] = {};
    set_header(buf, 'E', 3, 20'000'000);
    be64(buf + 11, 1001);                  // order_ref = 1001
    be32(buf + 19, 200);                   // executed_shares = 200
    be64(buf + 23, 50001);                 // match_number

    ItchParser parser;
    size_t off = 0;
    auto msg = parser.parse_one(buf, sizeof(buf), off);

    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->type, MessageType::Execute);
    EXPECT_EQ(msg->order_id, 1001);
    EXPECT_EQ(msg->qty, 200);
    EXPECT_EQ(msg->px, 0);                 // no price in 'E'
    EXPECT_EQ(off, sizeof(buf));
}

// ── 4. Parse Order Executed with Price (C) ─────────────────────────────────

TEST(ItchParserTest, ParseOrderExecutedPrice) {
    uint8_t buf[36] = {};
    set_header(buf, 'C', 4, 30'000'000);
    be64(buf + 11, 2002);
    be32(buf + 19, 150);                   // shares = 150
    be64(buf + 23, 50002);
    buf[31] = 'Y';                          // printable
    be32(buf + 32, 1'505'000);             // price = $150.50

    ItchParser parser;
    size_t off = 0;
    auto msg = parser.parse_one(buf, sizeof(buf), off);

    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->type, MessageType::Trade);
    EXPECT_EQ(msg->order_id, 2002);
    EXPECT_EQ(msg->qty, 150);
    EXPECT_EQ(msg->px, 1'505'000);
    EXPECT_EQ(off, sizeof(buf));
}

// ── 5. Parse Order Cancel (X) ──────────────────────────────────────────────

TEST(ItchParserTest, ParseOrderCancel) {
    uint8_t buf[23] = {};
    set_header(buf, 'X', 5, 40'000'000);
    be64(buf + 11, 3003);                  // order_ref
    be32(buf + 19, 100);                   // canceled_shares = 100

    ItchParser parser;
    size_t off = 0;
    auto msg = parser.parse_one(buf, sizeof(buf), off);

    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->type, MessageType::Delete);
    EXPECT_EQ(msg->order_id, 3003);
    EXPECT_EQ(msg->qty, 100);
    EXPECT_EQ(off, sizeof(buf));
}

// ── 6. Parse Order Delete (D) ──────────────────────────────────────────────

TEST(ItchParserTest, ParseOrderDelete) {
    uint8_t buf[19] = {};
    set_header(buf, 'D', 6, 50'000'000);
    be64(buf + 11, 4004);                  // order_ref

    ItchParser parser;
    size_t off = 0;
    auto msg = parser.parse_one(buf, sizeof(buf), off);

    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->type, MessageType::Delete);
    EXPECT_EQ(msg->order_id, 4004);
    EXPECT_EQ(msg->qty, 0);                // full delete
    EXPECT_EQ(off, sizeof(buf));
}

// ── 7. Parse Order Replace (U) ─────────────────────────────────────────────

TEST(ItchParserTest, ParseOrderReplace) {
    uint8_t buf[35] = {};  // sizeof(ItchOrderReplace)
    set_header(buf, 'U', 7, 60'000'000);
    be64(buf + 11, 5005);                  // original_order_ref
    be64(buf + 19, 6006);                  // new_order_ref
    be32(buf + 27, 300);                   // shares = 300
    be32(buf + 31, 1'510'000);             // price = $151.00

    ItchParser parser;
    size_t off = 0;
    auto msg = parser.parse_one(buf, sizeof(buf), off);

    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->type, MessageType::Replace);
    EXPECT_EQ(msg->order_id, 5005);
    EXPECT_EQ(msg->new_order_id, 6006);
    EXPECT_EQ(msg->qty, 300);
    EXPECT_EQ(msg->px, 1'510'000);
    EXPECT_EQ(off, sizeof(buf));
}

// ── 8. Parse Trade (P) ─────────────────────────────────────────────────────

TEST(ItchParserTest, ParseTrade) {
    uint8_t buf[44] = {};
    set_header(buf, 'P', 8, 70'000'000);
    be64(buf + 11, 7007);
    buf[19] = 'B';                          // buyer-initiated
    be32(buf + 20, 100);                    // shares = 100
    std::memcpy(buf + 24, "GOOG    ", 8);
    be32(buf + 32, 1'800'000);              // $180.00
    be64(buf + 36, 60001);                  // match_number

    ItchParser parser;
    size_t off = 0;
    auto msg = parser.parse_one(buf, sizeof(buf), off);

    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->type, MessageType::Trade);
    EXPECT_EQ(msg->order_id, 7007);
    EXPECT_EQ(msg->side, Side::Buy);
    EXPECT_EQ(msg->qty, 100);
    EXPECT_EQ(msg->px, 1'800'000);
    EXPECT_EQ(off, sizeof(buf));
}

// ── 9. Parse Cross Trade (Q) ───────────────────────────────────────────────

TEST(ItchParserTest, ParseCrossTrade) {
    uint8_t buf[36] = {};
    set_header(buf, 'Q', 9, 80'000'000);
    be32(buf + 11, 500);                    // shares = 500
    std::memcpy(buf + 15, "AAPL    ", 8);
    be32(buf + 23, 1'505'000);              // $150.50
    be64(buf + 27, 70001);
    buf[35] = 'O';                          // cross_type = Opening

    ItchParser parser;
    size_t off = 0;
    auto msg = parser.parse_one(buf, sizeof(buf), off);

    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->type, MessageType::Trade);
    EXPECT_EQ(msg->order_id, 0);            // crosses have no order_ref
    EXPECT_EQ(msg->qty, 500);
    EXPECT_EQ(msg->px, 1'505'000);
    EXPECT_EQ(off, sizeof(buf));
}

// ── 10. Parse System Event (S) ─────────────────────────────────────────────

TEST(ItchParserTest, ParseSystemEvent) {
    uint8_t buf[12] = {};
    set_header(buf, 'S', 10, 0);
    buf[11] = 'O';                          // start of messages

    ItchParser parser;
    size_t off = 0;
    auto msg = parser.parse_one(buf, sizeof(buf), off);

    ASSERT_FALSE(msg.has_value());          // system events → no BookMessage
    EXPECT_EQ(off, sizeof(buf));            // still advances offset
}

// ── 11. Invalid Message Type ───────────────────────────────────────────────

TEST(ItchParserTest, InvalidMessageType) {
    uint8_t buf[5] = {0xFF, 0, 0, 0, 0};   // unknown type

    ItchParser parser;
    size_t off = 0;
    auto msg = parser.parse_one(buf, sizeof(buf), off);

    ASSERT_FALSE(msg.has_value());
    EXPECT_EQ(off, 1);                      // skipped 1 byte
}

// ── 12. Sequence Gap Detection ─────────────────────────────────────────────

TEST(ItchParserTest, SequenceGapDetection) {
    uint8_t msg1[40] = {};
    set_header(msg1, 'A', 5, 0);            // track = 5 (bad — starts at wrong number)
    be64(msg1 + 11, 1);
    msg1[19] = 'B';
    be32(msg1 + 20, 100);
    std::memcpy(msg1 + 24, "AAPL    ", 8);
    be32(msg1 + 32, 1'000'000);

    uint8_t msg2[19] = {};
    set_header(msg2, 'D', 7, 0);            // track = 7 (gap: expected 6)
    be64(msg2 + 11, 1);

    ItchParser parser;

    // First message sets expected_ to 5
    size_t off = 0;
    parser.parse_one(msg1, sizeof(msg1), off);
    EXPECT_FALSE(parser.has_gap());         // no gap yet (first msg)

    // Second message: track=7, expected=6
    off = 0;
    parser.parse_one(msg2, sizeof(msg2), off);
    EXPECT_TRUE(parser.has_gap());
    EXPECT_EQ(parser.last_gap().expected, 6);
    EXPECT_EQ(parser.last_gap().actual, 7);
}

// ── 13. Full Order Lifecycle (Reconstructor Integration) ───────────────────

TEST(ItchParserTest, ReconstructorFullLifecycle) {
    // Add(id=1, px=100.00, qty=500, side=Buy)
    uint8_t add[40] = {};
    set_header(add, 'A', 1, 0);
    be64(add + 11, 1);
    add[19] = 'B';
    be32(add + 20, 500);
    std::memcpy(add + 24, "AAPL    ", 8);
    be32(add + 32, 1'000'000);

    // Cancel(id=1, shares=200)
    uint8_t cancel[23] = {};
    set_header(cancel, 'X', 2, 100'000'000);
    be64(cancel + 11, 1);
    be32(cancel + 19, 200);

    // Trade(id=1, px=100.00, shares=200, side=Sell)
    uint8_t trade[44] = {};
    set_header(trade, 'P', 3, 200'000'000);
    be64(trade + 11, 1);
    trade[19] = 'S';
    be32(trade + 20, 200);
    std::memcpy(trade + 24, "AAPL    ", 8);
    be32(trade + 32, 1'000'000);
    be64(trade + 36, 1001);

    // Delete(id=1)
    uint8_t del[19] = {};
    set_header(del, 'D', 4, 300'000'000);
    be64(del + 11, 1);

    ItchParser parser;
    Reconstructor rec;

    // 1. Add order
    size_t off = 0;
    auto msg = parser.parse_one(add, sizeof(add), off);
    ASSERT_TRUE(msg.has_value());
    rec.apply(*msg);
    EXPECT_EQ(rec.volume_at(1'000'000, Side::Buy), 500);
    auto ref = rec.lookup(1);
    ASSERT_TRUE(ref.has_value());
    EXPECT_EQ(ref->qty, 500);
    EXPECT_EQ(ref->price, 1'000'000);
    EXPECT_EQ(ref->side, Side::Buy);

    // 2. Cancel 200 shares
    off = 0;
    msg = parser.parse_one(cancel, sizeof(cancel), off);
    ASSERT_TRUE(msg.has_value());
    rec.apply(*msg);
    EXPECT_EQ(rec.volume_at(1'000'000, Side::Buy), 300);
    ref = rec.lookup(1);
    ASSERT_TRUE(ref.has_value());
    EXPECT_EQ(ref->qty, 300);

    // 3. Trade 200 shares (sell side hits bid)
    off = 0;
    msg = parser.parse_one(trade, sizeof(trade), off);
    ASSERT_TRUE(msg.has_value());
    rec.apply(*msg);
    EXPECT_EQ(rec.volume_at(1'000'000, Side::Buy), 100);
    ref = rec.lookup(1);
    ASSERT_TRUE(ref.has_value());
    EXPECT_EQ(ref->qty, 100);

    // 4. Delete remaining
    off = 0;
    msg = parser.parse_one(del, sizeof(del), off);
    ASSERT_TRUE(msg.has_value());
    rec.apply(*msg);
    EXPECT_EQ(rec.volume_at(1'000'000, Side::Buy), 0);
    EXPECT_FALSE(rec.lookup(1).has_value());
}

// ── 14. Replace Order (Reconstructor Integration) ──────────────────────────

TEST(ItchParserTest, ReconstructorReplace) {
    // Add(id=1, px=100.00, qty=500, side=Buy)
    uint8_t add[40] = {};
    set_header(add, 'A', 1, 0);
    be64(add + 11, 1);
    add[19] = 'B';
    be32(add + 20, 500);
    std::memcpy(add + 24, "AAPL    ", 8);
    be32(add + 32, 1'000'000);

    // Replace(id=1 → id=2, px=100.50, qty=300)
    uint8_t repl[37] = {};
    set_header(repl, 'U', 2, 100'000'000);
    be64(repl + 11, 1);
    be64(repl + 19, 2);
    be32(repl + 27, 300);
    be32(repl + 31, 1'005'000);             // $100.50

    ItchParser parser;
    Reconstructor rec;

    size_t off = 0;
    rec.apply(*parser.parse_one(add, sizeof(add), off));

    off = 0;
    rec.apply(*parser.parse_one(repl, sizeof(repl), off));

    // Old order gone
    EXPECT_FALSE(rec.lookup(1).has_value());
    // New price level has 300
    EXPECT_EQ(rec.volume_at(1'005'000, Side::Buy), 300);
    // Old price level should be empty
    EXPECT_EQ(rec.volume_at(1'000'000, Side::Buy), 0);
    // New order in map
    auto ref = rec.lookup(2);
    ASSERT_TRUE(ref.has_value());
    EXPECT_EQ(ref->price, 1'005'000);
    EXPECT_EQ(ref->qty, 300);
    EXPECT_EQ(ref->side, Side::Buy);
}

// ── 15. Replace at Same Price (aggregation test) ────────────────────────────

TEST(ItchParserTest, ReplaceAtSamePrice) {
    // Add order A (id=1, px=100.00, qty=200)
    // Add order B (id=2, px=100.00, qty=300)
    // Replace B (id=2 → id=3, px=100.00, qty=250)
    // Level should be 200 + 250 = 450

    auto make_add = [](uint64_t oid, uint64_t track, Price px, Qty qty) -> std::vector<uint8_t> {
        std::vector<uint8_t> buf(40, 0);
        set_header(buf.data(), 'A', static_cast<uint16_t>(track), 0);
        be64(buf.data() + 11, oid);
        buf[19] = 'B';
        be32(buf.data() + 20, static_cast<uint32_t>(qty));
        std::memcpy(buf.data() + 24, "AAPL    ", 8);
        be32(buf.data() + 32, static_cast<uint32_t>(px));
        return buf;
    };

    auto make_repl = [](uint64_t old_oid, uint64_t new_oid, uint64_t track, Price px, Qty qty) -> std::vector<uint8_t> {
        std::vector<uint8_t> buf(37, 0);
        set_header(buf.data(), 'U', static_cast<uint16_t>(track), 100'000);
        be64(buf.data() + 11, old_oid);
        be64(buf.data() + 19, new_oid);
        be32(buf.data() + 27, static_cast<uint32_t>(qty));
        be32(buf.data() + 31, static_cast<uint32_t>(px));
        return buf;
    };

    ItchParser parser;
    Reconstructor rec;

    auto a1 = make_add(1, 1, 1'000'000, 200);
    auto a2 = make_add(2, 2, 1'000'000, 300);
    auto r1 = make_repl(2, 3, 3, 1'000'000, 250);

    size_t off = 0;
    rec.apply(*parser.parse_one(a1.data(), a1.size(), off));
    off = 0;
    rec.apply(*parser.parse_one(a2.data(), a2.size(), off));

    EXPECT_EQ(rec.volume_at(1'000'000, Side::Buy), 500);

    off = 0;
    rec.apply(*parser.parse_one(r1.data(), r1.size(), off));

    // Level should be 200 + 250 = 450 (old B's 300 removed, new C's 250 added)
    EXPECT_EQ(rec.volume_at(1'000'000, Side::Buy), 450);
}

// ── 16. Batch Parse Multiple Messages ───────────────────────────────────────

TEST(ItchParserTest, BatchParse) {
    // Build a buffer with: Add + Add + Trade + Delete
    uint8_t buf[40 + 44 + 23 + 19] = {};

    // Add order 1 (Buy 100 @ 100.00)
    uint8_t* p = buf;
    set_header(p, 'A', 1, 0);
    be64(p + 11, 1); p[19] = 'B'; be32(p + 20, 100);
    std::memcpy(p + 24, "AAPL    ", 8); be32(p + 32, 1'000'000);

    // Trade (Sell 50 @ 100.00)
    p = buf + 40;
    set_header(p, 'P', 2, 100'000'000);
    be64(p + 11, 1); p[19] = 'S'; be32(p + 20, 50);
    std::memcpy(p + 24, "AAPL    ", 8); be32(p + 32, 1'000'000);
    be64(p + 36, 1001);

    // Cancel 30 shares
    p = buf + 40 + 44;
    set_header(p, 'X', 3, 200'000'000);
    be64(p + 11, 1); be32(p + 19, 30);

    // Delete remaining
    p = buf + 40 + 44 + 23;
    set_header(p, 'D', 4, 300'000'000);
    be64(p + 11, 1);

    ItchParser parser;
    auto result = parser.parse(buf, sizeof(buf));

    ASSERT_EQ(result.messages.size(), 4);
    EXPECT_EQ(result.messages[0].type, MessageType::Add);
    EXPECT_EQ(result.messages[1].type, MessageType::Trade);
    EXPECT_EQ(result.messages[2].type, MessageType::Delete);
    EXPECT_EQ(result.messages[3].type, MessageType::Delete);

    // Replay through reconstructor
    Reconstructor rec;
    for (auto& m : result.messages) {
        rec.apply(m);
    }
    EXPECT_TRUE(rec.bids().empty());
    EXPECT_FALSE(rec.lookup(1).has_value());
}
