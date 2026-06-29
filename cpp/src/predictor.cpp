#include <lob/predictor.hpp>

#ifdef LOB_ENABLE_PREDICTOR
#include <LightGBM/c_api.h>
#endif

namespace lob {

Predictor::~Predictor() {
#ifdef LOB_ENABLE_PREDICTOR
    if (booster_) {
        LGBM_BoosterFree(booster_);
    }
#endif
}

bool Predictor::load_model(const std::string& model_path) {
#ifdef LOB_ENABLE_PREDICTOR
    if (booster_) {
        LGBM_BoosterFree(booster_);
        booster_ = nullptr;
    }
    int num_iterations = 0;
    int result = LGBM_BoosterCreateFromModelfile(
        model_path.c_str(), &num_iterations, &booster_);
    return result == 0;
#else
    (void)model_path;
    return false;
#endif
}

double Predictor::predict(const std::array<double, 12>& features) {
#ifdef LOB_ENABLE_PREDICTOR
    if (!booster_) {
        return 0.0;
    }
    double out_result[1];
    int64_t out_len = 0;
    int result = LGBM_BoosterPredictForMat(
        booster_,
        features.data(),
        1,       // C_API_DTYPE_FLOAT64
        1,       // nrow
        12,      // ncol
        1,       // is_row_major
        0,       // C_API_PREDICT_NORMAL
        0,       // start_iteration
        -1,      // num_iteration (all)
        "",      // parameter
        &out_len,
        out_result);
    if (result != 0 || out_len < 1) {
        return 0.0;
    }
    return out_result[0];
#else
    (void)features;
    return 0.0;
#endif
}

std::vector<double> Predictor::predict_batch(
    const std::vector<std::array<double, 12>>& features) {
#ifdef LOB_ENABLE_PREDICTOR
    if (!booster_ || features.empty()) {
        return {};
    }
    std::vector<double> flat;
    flat.reserve(features.size() * 12);
    for (const auto& f : features) {
        flat.insert(flat.end(), f.begin(), f.end());
    }
    std::vector<double> results(features.size(), 0.0);
    int64_t out_len = 0;
    int result = LGBM_BoosterPredictForMat(
        booster_,
        flat.data(),
        1,       // C_API_DTYPE_FLOAT64
        static_cast<int32_t>(features.size()),
        12,      // ncol
        1,       // is_row_major
        0,       // C_API_PREDICT_NORMAL
        0,       // start_iteration
        -1,      // num_iteration (all)
        "",      // parameter
        &out_len,
        results.data());
    if (result != 0) {
        return {};
    }
    if (static_cast<size_t>(out_len) < features.size()) {
        results.resize(static_cast<size_t>(out_len));
    }
    return results;
#else
    (void)features;
    return {};
#endif
}

} // namespace lob
