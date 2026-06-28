#include <benchmark/benchmark.h>
#include "lob/itch_parser.hpp"
#include <cstring>
#include <vector>

using namespace lob;

// ── Helpers (defined before use) ───────────────────────────────────────────

static void set_header(uint8_t* buf, char msg_type, uint16_t track_no, uint64_t ts_ns) {
    buf[0] = static_cast<uint8_t>(msg_type);
    buf[1] = 0; buf[2] = 0;
    buf[3] = static_cast<uint8_t>(track_no >> 8);
    buf[4] = static_cast<uint8_t>(track_no);
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

// Build a single Add Order (A) message in a buffer
static void make_add_msg(uint8_t* buf, uint64_t track) {
    set_header(buf, 'A', static_cast<uint16_t>(track), 0);
    auto add = reinterpret_cast<ItchAddOrder*>(buf);
    add->order_ref[7] = static_cast<uint8_t>(track);
    add->buy_sell_indicator = 'B';
    be32(add->shares, 1000);
    std::memcpy(add->stock, "AAPL    ", 8);
    be32(add->price, 1'000'000);
}

// Generate a batch buffer with alternating message types
static std::vector<uint8_t> make_batch(size_t count) {
    constexpr size_t cycle_bytes = 40 + 44 + 23 + 19;
    std::vector<uint8_t> buf(count * cycle_bytes / 4 + 44);
    size_t off = 0;

    for (size_t i = 0; i < count / 4; ++i) {
        uint64_t base = i * 4;

        set_header(buf.data() + off, 'A', static_cast<uint16_t>(base + 1), 0);
        auto add = reinterpret_cast<ItchAddOrder*>(buf.data() + off);
        add->order_ref[7] = static_cast<uint8_t>(base + 1);
        add->buy_sell_indicator = 'B';
        be32(add->shares, 100);
        std::memcpy(add->stock, "AAPL    ", 8);
        be32(add->price, 1'000'000);
        off += 40;

        set_header(buf.data() + off, 'P', static_cast<uint16_t>(base + 2), 100'000);
        auto trade = reinterpret_cast<ItchTrade*>(buf.data() + off);
        trade->order_ref[7] = static_cast<uint8_t>(base + 1);
        trade->buy_sell_indicator = 'S';
        be32(trade->shares, 50);
        std::memcpy(trade->stock, "AAPL    ", 8);
        be32(trade->price, 1'000'000);
        be64(trade->match_number, base + 1000);
        off += 44;

        set_header(buf.data() + off, 'X', static_cast<uint16_t>(base + 3), 200'000);
        auto cancel = reinterpret_cast<ItchOrderCancel*>(buf.data() + off);
        cancel->order_ref[7] = static_cast<uint8_t>(base + 1);
        be32(cancel->canceled_shares, 30);
        off += 23;

        set_header(buf.data() + off, 'D', static_cast<uint16_t>(base + 4), 300'000);
        auto del = reinterpret_cast<ItchOrderDelete*>(buf.data() + off);
        del->order_ref[7] = static_cast<uint8_t>(base + 1);
        off += 19;
    }

    buf.resize(off);
    return buf;
}

// ── Benchmark 1: Single Add Order ──────────────────────────────────────────

static void BM_ItchParserSingleMsg(benchmark::State& state) {
    uint8_t buf[40] = {};
    make_add_msg(buf, 1);

    for (auto _ : state) {
        ItchParser parser;
        size_t off = 0;
        auto msg = parser.parse_one(buf, sizeof(buf), off);
        benchmark::DoNotOptimize(msg);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ItchParserSingleMsg);

// ── Benchmark 2: Batch Parse ───────────────────────────────────────────────

static void BM_ItchParserBatch(benchmark::State& state) {
    auto batch = make_batch(1000);

    for (auto _ : state) {
        ItchParser parser;
        auto result = parser.parse(batch.data(), batch.size());
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(1000LL * state.iterations());
    state.SetBytesProcessed(int64_t(batch.size()) * state.iterations());
}
BENCHMARK(BM_ItchParserBatch);

// ── Benchmark 3: ITCH throughput ───────────────────────────────────────────

static void BM_ItchVsJson(benchmark::State& state) {
    auto batch = make_batch(1000);
    int64_t bytes = batch.size();

    for (auto _ : state) {
        ItchParser parser;
        auto result = parser.parse(batch.data(), batch.size());
        benchmark::DoNotOptimize(result);
    }
    state.SetBytesProcessed(bytes * state.iterations());
    state.SetLabel("ITCH binary (1000 msgs)");
}
BENCHMARK(BM_ItchVsJson);

BENCHMARK_MAIN();
