#pragma once

#include "lob/types.hpp"
#include "lob/reconstructor.hpp"
#include "lob/feature_engine.hpp"

namespace lob {

struct OrderFillFeatures {
    // Order description
    Price order_price = 0;
    Qty   order_qty   = 0;
    Side  order_side  = Side::Buy;

    // Queue features
    Qty    depth_ahead      = 0;   // total qty at this price level (same side)
    double level_imbalance  = 0.0; // bid_qty / (bid_qty + ask_qty) at this price

    // Market features (copied from FeatureEngine)
    double top_imbalance    = 0.0;
    double spread           = 0.0;
    double midprice         = 0.0;
    double arrival_rate     = 0.0;
    double cancel_rate      = 0.0;
    int64_t ofi             = 0;

    // Labels (for training)
    bool fill_1ms  = false;
    bool fill_5ms  = false;
    bool fill_10ms = false;
};

class QueueModel {
public:
    static OrderFillFeatures extract_features(
        const Reconstructor& rec,
        const FeatureEngine& fe,
        Price order_price,
        Qty order_qty,
        Side order_side
    );
};

} // namespace lob
