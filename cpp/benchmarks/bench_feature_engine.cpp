#include <benchmark/benchmark.h>
#include "lob/feature_engine.hpp"
#include "lob/reconstructor.hpp"
#include <vector>
#include <random>

using namespace lob;

static void BM_FeatureEngineUpdate(benchmark::State& state) {
    int num_msgs = state.range(0);

    // Build a realistic book first, then measure feature computation
    Reconstructor rec;
    FeatureEngine fe;

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> qty_dist(1, 1000);

    // Pre-populate book
    for (int px = 1'000'000; px <= 1'100'000; px += 100) {
        auto bid_msg = BookMessage{.seq = 1, .ts_us = 1, .type = MessageType::Add,
            .px = px, .qty = static_cast<Qty>(qty_dist(rng)), .side = Side::Buy};
        auto ask_msg = BookMessage{.seq = 1, .ts_us = 1, .type = MessageType::Add,
            .px = px + 1000, .qty = static_cast<Qty>(qty_dist(rng)), .side = Side::Sell};
        rec.apply(bid_msg);
        rec.apply(ask_msg);
        fe.update(bid_msg, rec);
        fe.update(ask_msg, rec);
    }

    // Generate update messages
    std::vector<BookMessage> updates;
    updates.reserve(num_msgs);
    for (int i = 0; i < num_msgs; ++i) {
        Price px = 1'000'000 + (i % 1000) * 100;
        Side side = (i % 2 == 0) ? Side::Buy : Side::Sell;
        updates.push_back(BookMessage{
            .seq = static_cast<uint64_t>(i),
            .ts_us = static_cast<uint64_t>(i) * 10,
            .type = MessageType::Modify,
            .order_id = static_cast<uint64_t>(i),
            .px = px,
            .qty = static_cast<Qty>(qty_dist(rng)),
            .side = side,
        });
    }

    for (auto _ : state) {
        Reconstructor rec2;
        FeatureEngine fe2;

        // Rebuild book
        for (int px = 1'000'000; px <= 1'100'000; px += 100) {
            auto bid_msg = BookMessage{.seq = 1, .ts_us = 1, .type = MessageType::Add,
                .px = px, .qty = static_cast<Qty>(qty_dist(rng)), .side = Side::Buy};
            auto ask_msg = BookMessage{.seq = 1, .ts_us = 1, .type = MessageType::Add,
                .px = px + 1000, .qty = static_cast<Qty>(qty_dist(rng)), .side = Side::Sell};
            rec2.apply(bid_msg);
            rec2.apply(ask_msg);
            fe2.update(bid_msg, rec2);
            fe2.update(ask_msg, rec2);
        }

        for (const auto& m : updates) {
            rec2.apply(m);
            fe2.update(m, rec2);
        }

        benchmark::DoNotOptimize(fe2.current());
    }

    state.SetItemsProcessed(state.iterations() * num_msgs);
}
BENCHMARK(BM_FeatureEngineUpdate)->Arg(10000)->Arg(50000);

BENCHMARK_MAIN();
