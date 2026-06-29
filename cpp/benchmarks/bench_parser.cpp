#include <benchmark/benchmark.h>
#include "lob/binance_parser.hpp"
#include <sstream>
#include <random>

using namespace lob;

// Generate a depthUpdate JSON with N price levels per side
static std::string make_depth_update(uint64_t seq, int num_levels) {
    std::ostringstream json;
    json << R"({"e":"depthUpdate","E":)" << (seq * 1000) << R"(,"s":"BTCUSDT","U":)" << seq
         << R"(,"u":)" << seq;

    json << R"(,"b":[)";
    for (int i = 0; i < num_levels; ++i) {
        if (i > 0) json << ",";
        double px = 100.0 + (i * 0.01);
        double qty = 1.0 + (i % 100);
        json << R"([")" << px << R"(",")" << qty << R"("])";
    }

    json << R"(],"a":[)";
    for (int i = 0; i < num_levels; ++i) {
        if (i > 0) json << ",";
        double px = 101.0 + (i * 0.01);
        double qty = 1.0 + (i % 100);
        json << R"([")" << px << R"(",")" << qty << R"("])";
    }

    json << R"(]})";
    return json.str();
}

// Parser benchmark — pre-generate JSON outside timed loop
static void BM_BinanceParser(benchmark::State& state) {
    const int num_levels = state.range(0);
    const int batch_size = 1000;
    std::vector<std::string> batch(batch_size);
    for (int i = 0; i < batch_size; ++i) {
        batch[i] = make_depth_update(i + 1, num_levels);
    }
    int64_t items_parsed = 0;

    for (auto _ : state) {
        for (const auto& json : batch) {
            auto result = BinanceParser::parse_depth_update(json);
            items_parsed += result.messages.size();
            benchmark::DoNotOptimize(result);
        }
    }

    state.SetItemsProcessed(items_parsed);
    state.SetLabel(std::to_string(num_levels) + " levels per msg");
}
BENCHMARK(BM_BinanceParser)->Arg(5)->Arg(20)->Arg(100);

// Parser raw throughput (bytes processed)
static void BM_BinanceParserBytes(benchmark::State& state) {
    const int batch_size = 1000;
    std::vector<std::string> batch(batch_size);
    for (int i = 0; i < batch_size; ++i) {
        batch[i] = make_depth_update(i + 1, 20);
    }
    int64_t bytes = 0;

    for (auto _ : state) {
        for (const auto& json : batch) {
            bytes += json.size();
            auto result = BinanceParser::parse_depth_update(json);
            benchmark::DoNotOptimize(result);
        }
    }

    state.SetBytesProcessed(bytes);
}
BENCHMARK(BM_BinanceParserBytes);

BENCHMARK_MAIN();
