#include <gtest/gtest.h>
#include <lob/predictor.hpp>
#include <lob/reconstructor.hpp>
#include <lob/feature_engine.hpp>
#include <lob/snapshot.hpp>
#include <array>
#include <string>
#include <cmath>

using namespace lob;

#ifdef PROJECT_SOURCE_DIR
static std::string model_path() {
    return std::string(PROJECT_SOURCE_DIR) + "/models/btc/lightgbm_label_1.txt";
}
#else
static std::string model_path() {
    return "../models/btc/lightgbm_label_1.txt";
}
#endif

// Helper: push a BookMessage through rec + fe + se
static void push(Reconstructor& rec, FeatureEngine& fe,
                 SnapshotEngine& se, const BookMessage& msg) {
    rec.apply(msg);
    fe.update(msg, rec);
    se.check(rec, fe, msg.ts_us);
}

TEST(PredictorPipelineTest, NoPredictor_SnapshotsHaveNaN) {
    Reconstructor rec;
    FeatureEngine fe;
    SnapshotEngine se;

    BookMessage msg;
    msg.type = MessageType::Snapshot;
    msg.px = to_price(100.0);
    msg.qty = 10;
    msg.side = Side::Buy;
    msg.ts_us = 100'000;
    push(rec, fe, se, msg);

    msg.px = to_price(101.0);
    msg.side = Side::Sell;
    msg.qty = 5;
    msg.ts_us = 200'000;
    push(rec, fe, se, msg);

    ASSERT_GE(se.data().size(), 1);
    for (const auto& s : se.data()) {
        EXPECT_TRUE(std::isnan(s.prediction));
    }
}

TEST(PredictorPipelineTest, PredictorWithoutModel_SnapshotsHaveNaN) {
    Predictor pred;
    Reconstructor rec;
    FeatureEngine fe;
    SnapshotEngine se;
    se.set_predictor(&pred);

    BookMessage msg;
    msg.type = MessageType::Snapshot;
    msg.px = to_price(100.0);
    msg.qty = 10;
    msg.side = Side::Buy;
    msg.ts_us = 100'000;
    push(rec, fe, se, msg);

    msg.px = to_price(101.0);
    msg.side = Side::Sell;
    msg.qty = 5;
    msg.ts_us = 200'000;
    push(rec, fe, se, msg);

    ASSERT_GE(se.data().size(), 1);
    for (const auto& s : se.data()) {
        EXPECT_TRUE(std::isnan(s.prediction));
    }
}

TEST(PredictorPipelineTest, WithModel_SnapshotsHavePredictions) {
    Predictor pred;
    ASSERT_TRUE(pred.load_model(model_path()));
    ASSERT_TRUE(pred.is_loaded());

    Reconstructor rec;
    FeatureEngine fe;
    SnapshotEngine se;
    se.set_predictor(&pred);

    // Feed enough events to trigger at least one snapshot
    for (int i = 0; i < 5; ++i) {
        BookMessage msg;
        msg.type = MessageType::Snapshot;
        msg.seq = static_cast<uint64_t>(i + 1);
        msg.ts_us = static_cast<uint64_t>(100'000 * (i + 1));
        msg.px = to_price(100.0);
        msg.qty = 10;
        msg.side = Side::Buy;
        push(rec, fe, se, msg);

        msg.px = to_price(101.0);
        msg.side = Side::Sell;
        msg.qty = 5;
        msg.ts_us += 1;
        push(rec, fe, se, msg);
    }

    ASSERT_GE(se.data().size(), 1);
    for (const auto& s : se.data()) {
        EXPECT_FALSE(std::isnan(s.prediction));
        EXPECT_GE(s.prediction, 0.0);
        EXPECT_LE(s.prediction, 1.0);
    }
}

TEST(PredictorPipelineTest, PredictionChangesWithMarketState) {
    Predictor pred;
    ASSERT_TRUE(pred.load_model(model_path()));

    // First: market with tight spread, high imbalance
    Reconstructor rec;
    FeatureEngine fe;
    SnapshotEngine se;
    se.set_predictor(&pred);

    BookMessage msg;
    msg.type = MessageType::Snapshot;
    msg.ts_us = 100'000;
    msg.px = to_price(59940.0);
    msg.qty = 1000;
    msg.side = Side::Buy;
    push(rec, fe, se, msg);

    msg.px = to_price(59941.0);
    msg.side = Side::Sell;
    msg.qty = 100;
    msg.ts_us = 200'000;
    push(rec, fe, se, msg);

    ASSERT_GE(se.data().size(), 1);
    double pred1 = se.data().back().prediction;
    EXPECT_FALSE(std::isnan(pred1));

    // Second: widen spread, flip imbalance
    msg.type = MessageType::Snapshot;
    msg.px = to_price(59930.0);
    msg.qty = 100;
    msg.side = Side::Buy;
    msg.ts_us = 300'000;
    push(rec, fe, se, msg);

    msg.px = to_price(59950.0);
    msg.side = Side::Sell;
    msg.qty = 1000;
    msg.ts_us = 400'000;
    push(rec, fe, se, msg);

    ASSERT_GE(se.data().size(), 2);
    double pred2 = se.data().back().prediction;
    EXPECT_FALSE(std::isnan(pred2));

    // Predictions should differ (different market state)
    EXPECT_NE(pred1, pred2);
}
