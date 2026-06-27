#include <gtest/gtest.h>
#include "lob/reconstructor.hpp"

using namespace lob;

// ── Helpers ─────────────────────────────────────────────────────────────────

static BookMessage bid(MessageType type, Price px, Qty qty, uint64_t seq = 1) {
    return BookMessage{.seq = seq, .ts_us = seq, .type = type, .px = px, .qty = qty, .side = Side::Buy};
}

static BookMessage ask(MessageType type, Price px, Qty qty, uint64_t seq = 1) {
    return BookMessage{.seq = seq, .ts_us = seq, .type = type, .px = px, .qty = qty, .side = Side::Sell};
}

// ── Add ─────────────────────────────────────────────────────────────────────

TEST(ReconstructorTest, AddBid) {
    Reconstructor r;
    r.apply(bid(MessageType::Add, 1'005'000, 100));
    auto s = r.snapshot();
    EXPECT_EQ(s.bid.price, 1'005'000);
    EXPECT_EQ(s.bid.volume, 100);
    EXPECT_EQ(s.ask.price, 0);
}

TEST(ReconstructorTest, AddBothSides) {
    Reconstructor r;
    r.apply(bid(MessageType::Add, 1'005'000, 100));
    r.apply(ask(MessageType::Add, 1'006'000, 200));
    auto s = r.snapshot();
    EXPECT_EQ(s.bid.price, 1'005'000);
    EXPECT_EQ(s.bid.volume, 100);
    EXPECT_EQ(s.ask.price, 1'006'000);
    EXPECT_EQ(s.ask.volume, 200);
}

// ── Price ordering ──────────────────────────────────────────────────────────

TEST(ReconstructorTest, BidsSortedDescending) {
    Reconstructor r;
    r.apply(bid(MessageType::Add, 1'005'000, 100));   // lower price
    r.apply(bid(MessageType::Add, 1'006'000, 200));   // higher price — best bid
    auto s = r.snapshot();
    EXPECT_EQ(s.bid.price, 1'006'000);  // best bid is highest
}

TEST(ReconstructorTest, AsksSortedAscending) {
    Reconstructor r;
    r.apply(ask(MessageType::Add, 1'007'000, 100));
    r.apply(ask(MessageType::Add, 1'006'000, 200));   // lower price — best ask
    auto s = r.snapshot();
    EXPECT_EQ(s.ask.price, 1'006'000);
}

// ── Modify ──────────────────────────────────────────────────────────────────

TEST(ReconstructorTest, ModifyVolume) {
    Reconstructor r;
    r.apply(bid(MessageType::Add, 1'005'000, 100));
    r.apply(bid(MessageType::Modify, 1'005'000, 250));
    auto s = r.snapshot();
    EXPECT_EQ(s.bid.volume, 250);
}

TEST(ReconstructorTest, ModifyToZeroRemovesLevel) {
    Reconstructor r;
    r.apply(bid(MessageType::Add, 1'005'000, 100));
    r.apply(bid(MessageType::Modify, 1'005'000, 0));
    auto s = r.snapshot();
    EXPECT_EQ(s.bid.price, 0);
    EXPECT_TRUE(r.bids().empty());
}

// ── Delete ──────────────────────────────────────────────────────────────────

TEST(ReconstructorTest, DeleteLevel) {
    Reconstructor r;
    r.apply(bid(MessageType::Add, 1'005'000, 100));
    r.apply(ask(MessageType::Add, 1'006'000, 200));
    r.apply(bid(MessageType::Delete, 1'005'000, 0));
    auto s = r.snapshot();
    EXPECT_EQ(s.bid.price, 0);   // no bid
    EXPECT_EQ(s.ask.price, 1'006'000);  // ask unaffected
}

// ── Multiple levels ─────────────────────────────────────────────────────────

TEST(ReconstructorTest, MultipleLevels) {
    Reconstructor r;
    r.apply(bid(MessageType::Add, 100'499'000, 300));
    r.apply(bid(MessageType::Add, 100'500'000, 120));
    r.apply(ask(MessageType::Add, 100'501'000, 220));
    r.apply(ask(MessageType::Add, 100'502'000, 500));

    auto s = r.snapshot();
    EXPECT_EQ(s.bid.price, 100'500'000);
    EXPECT_EQ(s.bid.volume, 120);
    EXPECT_EQ(s.ask.price, 100'501'000);
    EXPECT_EQ(s.ask.volume, 220);

    EXPECT_EQ(r.bids().size(), 2);
    EXPECT_EQ(r.asks().size(), 2);
}

// ── Trade ───────────────────────────────────────────────────────────────────

TEST(ReconstructorTest, TradeReducesVolume) {
    Reconstructor r;
    r.apply(ask(MessageType::Add, 1'006'000, 500));
    r.apply(ask(MessageType::Trade, 1'006'000, 200));
    auto s = r.snapshot();
    EXPECT_EQ(s.ask.volume, 300);
}

TEST(ReconstructorTest, TradeExhaustsLevel) {
    Reconstructor r;
    r.apply(ask(MessageType::Add, 1'006'000, 200));
    r.apply(ask(MessageType::Trade, 1'006'000, 200));
    EXPECT_TRUE(r.asks().empty());
}

// ── Empty book ──────────────────────────────────────────────────────────────

TEST(ReconstructorTest, EmptyBook) {
    Reconstructor r;
    auto s = r.snapshot();
    EXPECT_EQ(s.bid.price, 0);
    EXPECT_EQ(s.ask.price, 0);
    EXPECT_EQ(s.seq, 0);
}

// ── Snapshot timestamps ─────────────────────────────────────────────────────

TEST(ReconstructorTest, SnapshotReflectsLastSeq) {
    Reconstructor r;
    r.apply(bid(MessageType::Add, 1'005'000, 100, 42));
    auto s = r.snapshot();
    EXPECT_EQ(s.seq, 42);
}
