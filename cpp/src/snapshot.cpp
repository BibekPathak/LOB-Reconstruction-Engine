#include "lob/snapshot.hpp"
#include "lob/predictor.hpp"

namespace lob {

void SnapshotEngine::check(const Reconstructor& rec, const FeatureEngine& fe, uint64_t ts_us) {
    ++msg_count_;

    bool time_trigger = first_ || (ts_us - last_ts_ >= TIME_THRESHOLD_US);
    bool count_trigger = (msg_count_ >= COUNT_THRESHOLD);

    if (time_trigger || count_trigger) {
        take(rec, fe, ts_us);
        last_ts_ = ts_us;
        msg_count_ = 0;
        first_ = false;
    }
}

std::array<double, 7> SnapshotEngine::features_from(const FeatureSet& f) const {
    return {f.midprice, f.spread, f.microprice,
            static_cast<double>(f.ofi),
            f.queue_imbalance, f.arrival_rate, f.cancel_rate};
}

void SnapshotEngine::take(const Reconstructor& rec, const FeatureEngine& fe, uint64_t ts_us) {
    auto snap = rec.snapshot();
    const auto& f = fe.current();

    MarketSnapshot ms;
    ms.seq            = snap.seq;
    ms.ts_us          = ts_us;
    ms.best_bid       = snap.bid.price;
    ms.best_bid_qty   = snap.bid.volume;
    ms.best_ask       = snap.ask.price;
    ms.best_ask_qty   = snap.ask.volume;
    ms.midprice       = f.midprice;
    ms.spread         = f.spread;
    ms.microprice     = f.microprice;
    ms.ofi            = f.ofi;
    ms.queue_imbalance = f.queue_imbalance;
    ms.arrival_rate   = f.arrival_rate;
    ms.cancel_rate    = f.cancel_rate;

    if (predictor_ && predictor_->is_loaded()) {
        ms.prediction = predictor_->predict(features_from(f));
    }

    snapshots_.push_back(ms);
}

void SnapshotEngine::reset() {
    first_ = true;
    last_ts_ = 0;
    msg_count_ = 0;
    snapshots_.clear();
}

} // namespace lob
