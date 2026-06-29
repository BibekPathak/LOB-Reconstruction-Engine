#include <gtest/gtest.h>
#include <lob/predictor.hpp>
#include <array>
#include <string>

using namespace lob;

#ifdef PROJECT_SOURCE_DIR
static std::string model_path() {
    return std::string(PROJECT_SOURCE_DIR) + "/../models/v2/lightgbm_label_1.txt";
}
#else
static std::string model_path() {
    return "../models/v2/lightgbm_label_1.txt";
}
#endif

TEST(PredictorTest, InitiallyNotLoaded) {
    Predictor p;
    EXPECT_FALSE(p.is_loaded());
}

TEST(PredictorTest, PredictWithoutModel) {
    Predictor p;
    std::array<double, 12> feats = {};
    EXPECT_DOUBLE_EQ(p.predict(feats), 0.0);
}

#ifdef LOB_ENABLE_PREDICTOR

TEST(PredictorTest, LoadModel_Success) {
    Predictor p;
    EXPECT_TRUE(p.load_model(model_path()));
    EXPECT_TRUE(p.is_loaded());
}

TEST(PredictorTest, LoadModel_FileNotFound) {
    Predictor p;
    EXPECT_FALSE(p.load_model("nonexistent_model.txt"));
    EXPECT_FALSE(p.is_loaded());
}

TEST(PredictorTest, PredictSingle_ReturnsProbabilityInRange) {
    Predictor p;
    ASSERT_TRUE(p.load_model(model_path()));
    std::array<double, 12> feats = {59942.995, 23.67, 59947.729, 41, 0.7, 156, 104, 0.0, 0.0, 0.0, 0.0, 0.0};
    double prob = p.predict(feats);
    EXPECT_GE(prob, 0.0);
    EXPECT_LE(prob, 1.0);
}

TEST(PredictorTest, PredictSingle_LabelOneRow) {
    Predictor p;
    ASSERT_TRUE(p.load_model(model_path()));
    std::array<double, 12> feats = {59940.85, 17.72, 59947.495, 7, 0.875, 291, 203, 0.0, 0.0, 0.0, 0.0, 0.0};
    double prob = p.predict(feats);
    EXPECT_GE(prob, 0.0);
    EXPECT_LE(prob, 1.0);
}

TEST(PredictorTest, PredictBatch_ReturnsAllResults) {
    Predictor p;
    ASSERT_TRUE(p.load_model(model_path()));
    std::array<double, 12> row0 = {59942.995, 23.67, 59947.729, 41, 0.7, 156, 104, 0.0, 0.0, 0.0, 0.0, 0.0};
    std::array<double, 12> row1 = {59940.85, 17.72, 59947.495, 7, 0.875, 291, 203, 0.0, 0.0, 0.0, 0.0, 0.0};
    auto results = p.predict_batch({row0, row1});
    ASSERT_EQ(results.size(), 2);
}

TEST(PredictorTest, PredictBatch_EmptyInput) {
    Predictor p;
    ASSERT_TRUE(p.load_model(model_path()));
    auto results = p.predict_batch({});
    EXPECT_TRUE(results.empty());
}

TEST(PredictorTest, Destructor_DoesNotCrash) {
    auto p = std::make_unique<Predictor>();
    ASSERT_TRUE(p->load_model(model_path()));
    std::array<double, 12> feats = {59942.995, 23.67, 59947.729, 41, 0.7, 156, 104, 0.0, 0.0, 0.0, 0.0, 0.0};
    double prob = p->predict(feats);
    EXPECT_GE(prob, 0.0);
    EXPECT_LE(prob, 1.0);
}

#endif
