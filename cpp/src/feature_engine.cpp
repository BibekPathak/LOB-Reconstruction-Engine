#include "lob/feature_engine.hpp"
#include <algorithm>
#include <cmath>

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
    // Count events in window
    return static_cast<double>(q.size());  // events per second since window == 1s
}

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

        // Microprice = (bid_px * ask_qty + ask_px * bid_qty) / (bid_qty + ask_qty)
        double total_qty = bid_qty + ask_qty;
        if (total_qty > 0) {
            current_.microprice = (bid_px * ask_qty + ask_px * bid_qty) / total_qty;
        } else {
            current_.microprice = current_.midprice;
        }

        // Queue imbalance = (bid_qty - ask_qty) / (bid_qty + ask_qty)
        current_.queue_imbalance = (bid_qty - ask_qty) / total_qty;
    }

    // ── OFI ─────────────────────────────────────────────────────────────
    // Compare this message's qty with the previous qty at the same level
    LevelKey key{msg.side, msg.px};
    Qty old_qty = 0;
    auto it = prev_qty_.find(key);
    if (it != prev_qty_.end()) {
        old_qty = it->second;
    }

    Qty delta = msg.qty - old_qty;
    if (msg.type == MessageType::Delete || msg.qty == 0) {
        delta = -old_qty;  // level removed
    }

    if (msg.side == Side::Buy) {
        if (delta > 0) current_.ofi += delta;
    } else {
        if (delta > 0) current_.ofi -= delta;
    }

    // Update previous qty
    if (msg.qty > 0 && msg.type != MessageType::Delete) {
        prev_qty_[key] = msg.qty;
    } else {
        prev_qty_.erase(key);
    }

    // ── Arrival / Cancel rates ──────────────────────────────────────────
    uint64_t now_us = msg.ts_us;

    if (msg.type == MessageType::Add) {
        arrival_events_.push_back(now_us);
    } else if (msg.type == MessageType::Delete) {
        cancel_events_.push_back(now_us);
    }

    prune_window(arrival_events_, now_us);
    prune_window(cancel_events_, now_us);

    current_.arrival_rate = compute_rate(arrival_events_, now_us);
    current_.cancel_rate  = compute_rate(cancel_events_, now_us);
}

void FeatureEngine::reset() {
    current_ = FeatureSet{};
    prev_qty_.clear();
    arrival_events_.clear();
    cancel_events_.clear();
}

} // namespace lob
