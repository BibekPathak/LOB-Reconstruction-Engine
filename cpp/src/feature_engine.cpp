#include "lob/feature_engine.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace lob {

void FeatureEngine::prune_window(std::deque<uint64_t>& q, uint64_t now_us) {
    uint64_t cutoff = now_us > RATE_WINDOW_US ? now_us - RATE_WINDOW_US : 0;
    while (!q.empty() && q.front() < cutoff) {
        q.pop_front();
    }
}

double FeatureEngine::compute_rate(const std::deque<uint64_t>& q, uint64_t now_us) const {
    if (q.empty()) return 0.0;
    uint64_t window_start = now_us > RATE_WINDOW_US ? now_us - RATE_WINDOW_US : 0;
    if (q.front() < window_start) return 0.0;
    return static_cast<double>(q.size());
}

// ── v2 feature helpers ─────────────────────────────────────────────────────

template<typename Compare>
static double compute_slope_impl(const std::map<Price, Qty, Compare>& levels, int n) {
    // Fit linear regression: level_index ~ log(volume)
    // Return slope coefficient (negative = volume concentrates at top)
    double sum_x = 0, sum_y = 0, sum_xy = 0, sum_x2 = 0;
    int count = 0;
    for (const auto& [px, qty] : levels) {
        if (count >= n) break;
        double x = static_cast<double>(count);
        double y = std::log(static_cast<double>(std::max<Qty>(qty, 1)));
        sum_x  += x;
        sum_y  += y;
        sum_xy += x * y;
        sum_x2 += x * x;
        ++count;
    }
    if (count < 2) return 0.0;
    double denom = count * sum_x2 - sum_x * sum_x;
    if (std::abs(denom) < 1e-12) return 0.0;
    return (count * sum_xy - sum_x * sum_y) / denom;
}

double FeatureEngine::compute_slope(const std::map<Price, Qty, std::greater<Price>>& levels, int n) {
    return compute_slope_impl(levels, n);
}

double FeatureEngine::compute_slope(const std::map<Price, Qty, std::less<Price>>& levels, int n) {
    return compute_slope_impl(levels, n);
}

static constexpr int SLOPE_LEVELS = 5;
static constexpr int VOLATILITY_WINDOW = 10;

double FeatureEngine::compute_volatility() const {
    if (midprice_history_.size() < 3) return 0.0;
    // Compute std of log returns
    std::vector<double> returns;
    returns.reserve(midprice_history_.size() - 1);
    for (size_t i = 1; i < midprice_history_.size(); ++i) {
        if (midprice_history_[i - 1] > 0 && midprice_history_[i] > 0) {
            returns.push_back(std::log(midprice_history_[i] / midprice_history_[i - 1]));
        }
    }
    if (returns.size() < 2) return 0.0;
    double mean = 0;
    for (double r : returns) mean += r;
    mean /= static_cast<double>(returns.size());
    double var = 0;
    for (double r : returns) var += (r - mean) * (r - mean);
    var /= static_cast<double>(returns.size() - 1);
    return std::sqrt(var);
}

// ── Main update ────────────────────────────────────────────────────────────

void FeatureEngine::update(const BookMessage& msg, const Reconstructor& rec) {
    // ── Level-based features (need both sides present) ─────────────────
    auto snap = rec.snapshot();
    bool has_bid = snap.bid.price != 0;
    bool has_ask = snap.ask.price != 0;

    if (has_bid && has_ask) {
        double bid_px = to_double(snap.bid.price);
        double ask_px = to_double(snap.ask.price);
        double bid_qty = static_cast<double>(snap.bid.volume);
        double ask_qty = static_cast<double>(snap.ask.volume);

        current_.midprice   = (bid_px + ask_px) / 2.0;
        current_.spread     = ask_px - bid_px;

        double total_qty = bid_qty + ask_qty;
        if (total_qty > 0) {
            current_.microprice = (bid_px * ask_qty + ask_px * bid_qty) / total_qty;
        } else {
            current_.microprice = current_.midprice;
        }

        current_.queue_imbalance = (bid_qty - ask_qty) / total_qty;

        // ── v2: book slope ─────────────────────────────────────────
        current_.bid_slope = compute_slope(rec.bids(), SLOPE_LEVELS);
        current_.ask_slope = compute_slope(rec.asks(), SLOPE_LEVELS);

        // ── v2: volatility via midprice history ─────────────────────
        if (current_.midprice > 0) {
            midprice_history_.push_back(current_.midprice);
            if (midprice_history_.size() > VOLATILITY_WINDOW) {
                midprice_history_.pop_front();
            }
        }
        current_.volatility = compute_volatility();
    }

    // ── OFI ──────────────────────────────────────────────────────────────
    LevelKey key{msg.side, msg.px};
    Qty old_qty = 0;
    auto it = prev_qty_.find(key);
    if (it != prev_qty_.end()) {
        old_qty = it->second;
    }

    Qty delta = msg.qty - old_qty;
    if (msg.type == MessageType::Delete || msg.qty == 0) {
        delta = -old_qty;
    }

    if (msg.side == Side::Buy) {
        if (delta > 0) current_.ofi += delta;
    } else {
        if (delta > 0) current_.ofi -= delta;
    }

    if (msg.qty > 0 && msg.type != MessageType::Delete) {
        prev_qty_[key] = msg.qty;
    } else {
        prev_qty_.erase(key);
    }

    // ── Arrival / Cancel / Trade rates ────────────────────────────────
    uint64_t now_us = msg.ts_us;

    if (msg.type == MessageType::Add) {
        arrival_events_.push_back(now_us);
    } else if (msg.type == MessageType::Delete) {
        cancel_events_.push_back(now_us);
    } else if (msg.type == MessageType::Trade || msg.type == MessageType::Execute) {
        trade_events_.push_back(now_us);
        // For ITCH: if order_id is present, look up the resting order side.
        // The trade aggressor is the opposite of the resting order.
        // A buy-initiated trade lifts the ask (resting side == Sell).
        if (msg.order_id != 0) {
            auto ref = rec.lookup(msg.order_id);
            if (ref && ref->side == Side::Sell) {
                buy_trade_events_.push_back(now_us);
            }
        } else {
            // Binance path: trade at best ask means buyer-initiated
            if (has_bid && has_ask) {
                double trade_px = to_double(msg.px);
                double mid = current_.midprice;
                if (trade_px >= mid) {
                    buy_trade_events_.push_back(now_us);
                }
            }
        }
    }

    prune_window(arrival_events_, now_us);
    prune_window(cancel_events_, now_us);
    prune_window(trade_events_, now_us);
    prune_window(buy_trade_events_, now_us);

    current_.arrival_rate    = compute_rate(arrival_events_, now_us);
    current_.cancel_rate     = compute_rate(cancel_events_, now_us);
    current_.trade_intensity = compute_rate(trade_events_, now_us);

    size_t total_trades = trade_events_.size();
    if (total_trades > 0) {
        current_.buy_ratio = static_cast<double>(buy_trade_events_.size())
                           / static_cast<double>(total_trades);
    }
}

void FeatureEngine::reset() {
    current_ = FeatureSet{};
    prev_qty_.clear();
    arrival_events_.clear();
    cancel_events_.clear();
    trade_events_.clear();
    buy_trade_events_.clear();
    midprice_history_.clear();
}

} // namespace lob
