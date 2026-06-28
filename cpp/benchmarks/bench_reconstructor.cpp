#include <benchmark/benchmark.h>
#include "lob/reconstructor.hpp"
#include <vector>
#include <random>

using namespace lob;

static std::vector<BookMessage> generate_messages(int count) {
    std::vector<BookMessage> msgs;
    msgs.reserve(count);
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> price_dist(1'000'000, 1'100'000);
    std::uniform_int_distribution<int> qty_dist(1, 1000);
    std::uniform_int_distribution<int> side_dist(0, 1);

    Price prev_bid = 0, prev_ask = 0;
    Qty prev_bid_qty = 0, prev_ask_qty = 0;

    for (int i = 0; i < count; ++i) {
        MessageType type;
        Price px;
        Qty qty;
        Side side = (side_dist(rng) == 0) ? Side::Buy : Side::Sell;

        if (i < count / 2) {
            // Phase 1: Add messages to build a book
            type = MessageType::Add;
            px = price_dist(rng);
            qty = qty_dist(rng);
        } else {
            // Phase 2: Mix of Modify and Delete
            int r = rng() % 3;
            if (r == 0) {
                type = MessageType::Add;
                px = price_dist(rng);
                qty = qty_dist(rng);
            } else if (r == 1) {
                type = MessageType::Modify;
                px = price_dist(rng);
                qty = qty_dist(rng);
            } else {
                type = MessageType::Delete;
                px = price_dist(rng);
                qty = 0;
            }
        }

        msgs.push_back(BookMessage{
            .seq = static_cast<uint64_t>(i),
            .ts_us = static_cast<uint64_t>(i) * 100,
            .type = type,
            .order_id = static_cast<uint64_t>(i),
            .px = px,
            .qty = qty,
            .side = side,
        });
    }

    return msgs;
}

static void BM_ReconstructorApply(benchmark::State& state) {
    int num_msgs = state.range(0);
    auto msgs = generate_messages(num_msgs);

    for (auto _ : state) {
        Reconstructor rec;
        for (const auto& m : msgs) {
            rec.apply(m);
        }
        benchmark::DoNotOptimize(rec.snapshot());
    }

    state.SetItemsProcessed(state.iterations() * num_msgs);
    state.SetLabel(std::to_string(num_msgs) + " msgs");
}

BENCHMARK(BM_ReconstructorApply)->Arg(10000)->Arg(100000)->Arg(500000);

static void BM_ReconstructorVolumeAt(benchmark::State& state) {
    Reconstructor rec;
    auto msgs = generate_messages(50000);
    for (const auto& m : msgs) rec.apply(m);

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> px_dist(1'000'000, 1'100'000);
    std::uniform_int_distribution<int> side_dist(0, 1);

    for (auto _ : state) {
        Price px = px_dist(rng);
        Side side = (side_dist(rng) == 0) ? Side::Buy : Side::Sell;
        auto vol = rec.volume_at(px, side);
        benchmark::DoNotOptimize(vol);
    }
}
BENCHMARK(BM_ReconstructorVolumeAt);

BENCHMARK_MAIN();
