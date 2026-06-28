#include "lob/queue_model.hpp"

namespace lob {

OrderFillFeatures QueueModel::extract_features(
    const Reconstructor& rec,
    const FeatureEngine& fe,
    Price order_price,
    Qty order_qty,
    Side order_side)
{
    OrderFillFeatures f;
    f.order_price = order_price;
    f.order_qty   = order_qty;
    f.order_side  = order_side;

    // Depth at this price level
    f.depth_ahead = rec.volume_at(order_price, order_side);

    // Level imbalance: bid vs ask volume at this specific price
    Qty bid_vol_at = rec.volume_at(order_price, Side::Buy);
    Qty ask_vol_at = rec.volume_at(order_price, Side::Sell);
    double total = static_cast<double>(bid_vol_at + ask_vol_at);
    f.level_imbalance = total > 0
        ? static_cast<double>(bid_vol_at) / total
        : 0.5;

    // Market features from feature engine
    const auto& feat = fe.current();
    f.top_imbalance   = feat.queue_imbalance;
    f.spread          = feat.spread;
    f.midprice        = feat.midprice;
    f.arrival_rate    = feat.arrival_rate;
    f.cancel_rate     = feat.cancel_rate;
    f.ofi             = feat.ofi;

    return f;
}

} // namespace lob
