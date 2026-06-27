#include <gtest/gtest.h>
#include "lob/binance_parser.hpp"
#include "lob/normalizer.hpp"
#include "lob/reconstructor.hpp"
#include "lob/feature_engine.hpp"
#include "lob/snapshot.hpp"
#include "lob/dataset_builder.hpp"
#include <cstdio>

using namespace lob;

TEST(DatasetPipelineTest, FullPipelineToCsv) {
    const char* outfile = "/tmp/lob_test_pipeline.csv";

    // Snapshot
    std::string snap = R"({
        "lastUpdateId": 1000,
        "bids": [["100.50","2.3"],["100.49","3.0"]],
        "asks": [["100.51","1.1"],["100.52","5.0"]]
    })";

    Normalizer norm;
    Reconstructor rec;
    FeatureEngine fe;
    SnapshotEngine se;

    for (const auto& m : norm.process_snapshot(BinanceParser::parse_snapshot(snap))) {
        rec.apply(m);
        fe.update(m, rec);
        se.check(rec, fe, m.ts_us);
    }

    // Depth update 1
    std::string upd1 = R"({
        "e":"depthUpdate","E":100000,"s":"BTCUSDT","U":1001,"u":1001,
        "b":[["100.51","1.0"],["100.49","0.0"]],
        "a":[["100.51","2.0"]]
    })";
    auto r1 = BinanceParser::parse_depth_update(upd1);
    for (const auto& m : norm.process(r1)) {
        rec.apply(m);
        fe.update(m, rec);
        se.check(rec, fe, m.ts_us);
    }

    // Advance time past threshold
    std::string upd2 = R"({
        "e":"depthUpdate","E":250000,"s":"BTCUSDT","U":1002,"u":1002,
        "b":[["100.50","2.0"]],
        "a":[]
    })";
    auto r2 = BinanceParser::parse_depth_update(upd2);
    for (const auto& m : norm.process(r2)) {
        rec.apply(m);
        fe.update(m, rec);
        se.check(rec, fe, m.ts_us);
    }

    // Write to CSV
    ASSERT_GE(se.data().size(), 2);  // at least 2 snapshots taken
    {
        DatasetBuilder db(outfile);
        db.write_all(se.data());
        db.close();
    }

    // Verify CSV has content
    std::ifstream f(outfile);
    std::string line;
    int count = 0;
    while (std::getline(f, line)) ++count;
    f.close();

    // Header + N data rows
    EXPECT_EQ(count, static_cast<int>(se.data().size()) + 1);

    std::remove(outfile);
}
