#pragma once

#include <string>
#include <vector>
#include <array>
#include <cstdint>

namespace lob {

class Predictor {
public:
    Predictor() = default;
    ~Predictor();

    Predictor(const Predictor&) = delete;
    Predictor& operator=(const Predictor&) = delete;
    Predictor(Predictor&&) = delete;
    Predictor& operator=(Predictor&&) = delete;

    bool load_model(const std::string& model_path);
    double predict(const std::array<double, 12>& features);
    std::vector<double> predict_batch(
        const std::vector<std::array<double, 12>>& features);

    bool is_loaded() const { return booster_ != nullptr; }

private:
    void* booster_ = nullptr;
};

} // namespace lob
