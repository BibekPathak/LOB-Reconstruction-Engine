#pragma once

#include "lob/types.hpp"
#include "lob/reconstructor.hpp"
#include <deque>
#include <unordered_map>
#include <cstddef>

namespace lob {

struct FeatureSet {
    double midprice       = 0.0;
    double spread         = 0.0;
    double microprice     = 0.0;
    int64_t ofi           = 0;
    double queue_imbalance = 0.0;
    double arrival_rate   = 0.0;
    double cancel_rate    = 0.0;
};

class FeatureEngine {
public:
    static constexpr uint64_t RATE_WINDOW_US = 1'000'000; // 1 second

    FeatureEngine() = default;

    // Call once per BookMessage, before applying to the Reconstructor
    // (needs old book state for OFI delta computation)
    void update(const BookMessage& msg, const Reconstructor& rec);

    const FeatureSet& current() const noexcept { return current_; }

    void reset();

private:
    FeatureSet current_;

    // ── OFI tracking ────────────────────────────────────────────────────
    // Previous quantity at each (side, price) so we can compute deltas
    struct LevelKey {
        Side  side;
        Price px;

        bool operator==(const LevelKey& o) const noexcept {
            return side == o.side && px == o.px;
        }
    };
    struct LevelKeyHash {
        size_t operator()(const LevelKey& k) const noexcept {
            uint64_t s = (k.side == Side::Sell) ? (1ULL << 63) : 0;
            return std::hash<uint64_t>{}(s | static_cast<uint64_t>(k.px));
        }
    };
    std::unordered_map<LevelKey, Qty, LevelKeyHash> prev_qty_;

    // ── Rate tracking ───────────────────────────────────────────────────
    std::deque<uint64_t> arrival_events_;
    std::deque<uint64_t> cancel_events_;

    void prune_window(std::deque<uint64_t>& q, uint64_t now_us);
    double compute_rate(const std::deque<uint64_t>& q, uint64_t now_us) const;
};

} // namespace lob
