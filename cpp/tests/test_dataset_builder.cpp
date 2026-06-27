#include <gtest/gtest.h>
#include "lob/dataset_builder.hpp"
#include "lob/snapshot.hpp"
#include <cstdio>
#include <sstream>
#include <fstream>

using namespace lob;

TEST(DatasetBuilderTest, WritesHeaderAndRow) {
    const char* tmpfile = "/tmp/test_dataset.csv";

    {
        DatasetBuilder db(tmpfile);

        MarketSnapshot s;
        s.seq = 42;
        s.ts_us = 1'000'000;
        s.best_bid = 1'005'000;
        s.best_bid_qty = 200;
        s.best_ask = 1'007'000;
        s.best_ask_qty = 100;
        s.midprice = 100.60;
        s.spread = 0.20;
        s.microprice = 100.6333;
        s.ofi = 150;
        s.queue_imbalance = 0.3333;
        s.arrival_rate = 5.0;
        s.cancel_rate = 2.0;

        db.write(s);
        db.close();
    }

    std::ifstream f(tmpfile);
    ASSERT_TRUE(f.is_open());
    std::string line1, line2;
    std::getline(f, line1);
    std::getline(f, line2);

    // Header
    EXPECT_TRUE(line1.find("seq") != std::string::npos);
    EXPECT_TRUE(line1.find("midprice") != std::string::npos);
    EXPECT_TRUE(line1.find("ofi") != std::string::npos);

    // Data row
    EXPECT_TRUE(line2.find("42,") != std::string::npos);
    EXPECT_TRUE(line2.find("100.6") != std::string::npos);

    std::remove(tmpfile);
}

TEST(DatasetBuilderTest, WriteAll) {
    const char* tmpfile = "/tmp/test_dataset_all.csv";

    {
        DatasetBuilder db(tmpfile);
        std::vector<MarketSnapshot> snaps(3);
        snaps[0].seq = 1;
        snaps[1].seq = 2;
        snaps[2].seq = 3;
        db.write_all(snaps);
        db.close();
    }

    std::ifstream f(tmpfile);
    std::string line;
    int line_count = 0;
    while (std::getline(f, line)) {
        ++line_count;
    }
    EXPECT_EQ(line_count, 4);  // header + 3 rows

    std::remove(tmpfile);
}

TEST(DatasetBuilderTest, EmptyFile) {
    const char* tmpfile = "/tmp/test_dataset_empty.csv";

    {
        DatasetBuilder db(tmpfile);
        db.close();
    }

    std::ifstream f(tmpfile);
    std::string line;
    bool has_content = static_cast<bool>(std::getline(f, line));
    EXPECT_FALSE(has_content);  // nothing written, no header

    std::remove(tmpfile);
}
