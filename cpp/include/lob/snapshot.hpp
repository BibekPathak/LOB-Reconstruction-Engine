#pragma once

#include "lob/types.hpp"
#include "lob/reconstructor.hpp"
#include "lob/feature_engine.hpp"
#include <vector>

namespace lob {

// A full market snapshot with book state + all features
struct MarketSnapshot {
    uint64_t seq        = 0;
    uint64_t ts_us      = 0;

    Price    best_bid   = 0;
    Qty      best_bid_qty = 0;
    Price    best_ask   = 0;
    Qty      best_ask_qty = 0;

    double   midprice       = 0.0;
    double   spread         = 0.0;
    double   microprice     = 0.0;
    int64_t  ofi            = 0;
    double   queue_imbalance = 0.0;
    double   arrival_rate   = 0.0;
    double   cancel_rate    = 0.0;
};

class SnapshotEngine {
public:
    static constexpr uint64_t TIME_THRESHOLD_US = 100'000;  // 100ms
    static constexpr uint64_t COUNT_THRESHOLD   = 1000;     // 1000 messages

    SnapshotEngine() = default;

    // Check thresholds and take snapshot if either is exceeded.
    // Call after each reconstructor apply + feature engine update.
    void check(const Reconstructor& rec, const FeatureEngine& fe, uint64_t ts_us);

    const std::vector<MarketSnapshot>& data() const noexcept { return snapshots_; }

    void reset();

private:
    bool first_ = true;
    uint64_t last_ts_  = 0;
    uint64_t msg_count_ = 0;
    std::vector<MarketSnapshot> snapshots_;

    void take(const Reconstructor& rec, const FeatureEngine& fe, uint64_t ts_us);
};

} // namespace lob
