#include <benchmark/benchmark.h>
#include "lob/binance_parser.hpp"
#include "lob/normalizer.hpp"
#include "lob/reconstructor.hpp"
#include "lob/feature_engine.hpp"
#include "lob/snapshot.hpp"
#include <sstream>
#include <vector>

using namespace lob;

// Generate a depthUpdate JSON string
static std::string make_update(uint64_t seq, int levels) {
    std::ostringstream j;
    j << R"({"e":"depthUpdate","E":)" << (seq * 1000) << R"(,"s":"BTCUSDT","U":)" << seq
      << R"(,"u":)" << seq;
    j << R"(,"b":[)";
    for (int i = 0; i < levels; ++i) {
        if (i) j << ",";
        j << R"([")" << (100.0 + i * 0.01) << R"(",")" << (10 + i) << R"("])";
    }
    j << R"(],"a":[)";
    for (int i = 0; i < levels; ++i) {
        if (i) j << ",";
        j << R"([")" << (101.0 + i * 0.01) << R"(",")" << (10 + i) << R"("])";
    }
    j << R"(]})";
    return j.str();
}

static void BM_FullPipeline(benchmark::State& state) {
    int levels = state.range(0);

    // Pre-generate a batch of JSON messages
    const int batch_size = 100;
    std::vector<std::string> json_batch(batch_size);
    for (int i = 0; i < batch_size; ++i) {
        json_batch[i] = make_update(1000 + i, levels);
    }

    int64_t total_msgs = 0;

    for (auto _ : state) {
        Normalizer norm;
        Reconstructor rec;
        FeatureEngine fe;
        SnapshotEngine se;

        for (const auto& json : json_batch) {
            auto parsed = BinanceParser::parse_depth_update(json);
            auto msgs = norm.process(parsed);
            total_msgs += msgs.size();

            for (const auto& m : msgs) {
                rec.apply(m);
                fe.update(m, rec);
                se.check(rec, fe, m.ts_us);
            }
        }

        benchmark::DoNotOptimize(se.data());
    }

    state.SetItemsProcessed(total_msgs);
    state.SetLabel(std::to_string(levels) + " levels");
}
BENCHMARK(BM_FullPipeline)->Arg(5)->Arg(20)->Arg(100);

BENCHMARK_MAIN();
