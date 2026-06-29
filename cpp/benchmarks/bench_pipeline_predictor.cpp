#include <benchmark/benchmark.h>
#include <lob/predictor.hpp>
#include <lob/reconstructor.hpp>
#include <lob/feature_engine.hpp>
#include <lob/snapshot.hpp>
#include <lob/binance_parser.hpp>
#include <lob/normalizer.hpp>
#include <string>
#include <vector>

using namespace lob;

#ifdef CMAKE_SOURCE_DIR
static std::string model_path() {
    return std::string(CMAKE_SOURCE_DIR) + "/../models/v2/lightgbm_label_1.txt";
}
#else
static std::string model_path() {
    return "../models/v2/lightgbm_label_1.txt";
}
#endif

// Build a realistic snapshot + depth update sequence
static std::vector<BookMessage> make_test_messages(int n) {
    std::vector<BookMessage> msgs;
    Normalizer norm;

    std::string snap = R"({
        "lastUpdateId": 1000,
        "bids": [["59940.00","2.3"],["59939.00","3.0"],["59938.00","1.5"]],
        "asks": [["59941.00","1.1"],["59942.00","5.0"]]
    })";
    for (const auto& m : norm.process_snapshot(BinanceParser::parse_snapshot(snap))) {
        msgs.push_back(m);
    }

    for (int i = 0; i < n; ++i) {
        std::string upd = R"({
            "e":"depthUpdate","E":)" + std::to_string(100'000 + i) + R"(,"s":"BTCUSDT",
            "U":)" + std::to_string(1001 + i) + R"(,"u":)" + std::to_string(1001 + i) + R"(,
            "b":[["59940.00","2.0"]],
            "a":[["59941.00","1.5"]]
        })";
        auto result = BinanceParser::parse_depth_update(upd);
        for (const auto& m : norm.process(result)) {
            msgs.push_back(m);
        }
    }
    return msgs;
}

static void BM_PipelineWithPrediction(benchmark::State& state) {
    Predictor pred;
    pred.load_model(model_path());
    auto msgs = make_test_messages(state.range(0));

    for (auto _ : state) {
        Reconstructor rec;
        FeatureEngine fe;
        SnapshotEngine se;
        se.set_predictor(&pred);

        for (const auto& m : msgs) {
            rec.apply(m);
            fe.update(m, rec);
            se.check(rec, fe, m.ts_us);
        }
        benchmark::DoNotOptimize(se.data().size());
    }
}
BENCHMARK(BM_PipelineWithPrediction)->Arg(100)->Arg(1000)->Arg(10000);

static void BM_PipelineWithoutPrediction(benchmark::State& state) {
    auto msgs = make_test_messages(state.range(0));

    for (auto _ : state) {
        Reconstructor rec;
        FeatureEngine fe;
        SnapshotEngine se;

        for (const auto& m : msgs) {
            rec.apply(m);
            fe.update(m, rec);
            se.check(rec, fe, m.ts_us);
        }
        benchmark::DoNotOptimize(se.data().size());
    }
}
BENCHMARK(BM_PipelineWithoutPrediction)->Arg(100)->Arg(1000)->Arg(10000);

BENCHMARK_MAIN();
