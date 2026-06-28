#include <benchmark/benchmark.h>
#include <lob/predictor.hpp>
#include <array>
#include <vector>
#include <string>

using namespace lob;

#ifdef CMAKE_SOURCE_DIR
static std::string model_path() {
    return std::string(CMAKE_SOURCE_DIR) + "/models/btc/lightgbm_label_1.txt";
}
#else
static std::string model_path() {
    return "../models/btc/lightgbm_label_1.txt";
}
#endif

static void BM_Predictor_LoadModel(benchmark::State& state) {
    for (auto _ : state) {
        Predictor p;
        p.load_model(model_path());
        benchmark::DoNotOptimize(p.is_loaded());
    }
}
BENCHMARK(BM_Predictor_LoadModel)->Iterations(10);

static void BM_Predictor_SinglePrediction(benchmark::State& state) {
    Predictor p;
    p.load_model(model_path());
    std::array<double, 7> feats = {59942.995, 23.67, 59947.729, 41, 0.7, 156, 104};
    for (auto _ : state) {
        auto result = p.predict(feats);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_Predictor_SinglePrediction);

static void BM_Predictor_BatchPrediction(benchmark::State& state) {
    Predictor p;
    p.load_model(model_path());
    const int N = state.range(0);
    std::vector<std::array<double, 7>> batch(
        N, std::array<double, 7>{59942.995, 23.67, 59947.729, 41, 0.7, 156, 104});
    for (auto _ : state) {
        auto results = p.predict_batch(batch);
        benchmark::DoNotOptimize(results.data());
    }
}
BENCHMARK(BM_Predictor_BatchPrediction)
    ->Arg(1)->Arg(10)->Arg(100)->Arg(1000);

BENCHMARK_MAIN();
