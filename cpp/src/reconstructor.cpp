#include "lob/reconstructor.hpp"

namespace lob {

void Reconstructor::apply(const BookMessage& msg) {
    seq_ = msg.ts_us;

    auto cleanup = [](auto& bk, Price px) {
        auto it = bk.find(px);
        if (it != bk.end() && it->second <= 0) {
            bk.erase(it);
        }
    };

    // Helper: apply to the correct ladder based on side
    auto with_ladder = [&](Side s, auto&& fn) -> void {
        if (s == Side::Buy) fn(bids_); else fn(asks_);
    };

    switch (msg.type) {

    case MessageType::Add:
        if (msg.qty > 0) {
            with_ladder(msg.side, [&](auto& ladder) { ladder[msg.px] += msg.qty; });
            if (msg.order_id != 0) {
                orders_[msg.order_id] = {msg.px, msg.qty, msg.side};
            }
        }
        break;

    case MessageType::Modify:
        with_ladder(msg.side, [&](auto& ladder) {
            if (msg.qty > 0) {
                ladder[msg.px] = msg.qty;
            } else {
                ladder.erase(msg.px);
            }
        });
        if (msg.order_id != 0) {
            auto it = orders_.find(msg.order_id);
            if (it != orders_.end()) {
                it->second.qty = msg.qty;
                if (msg.qty <= 0) orders_.erase(it);
            }
        }
        break;

    case MessageType::Delete:
        if (msg.order_id != 0) {
            auto it = orders_.find(msg.order_id);
            if (it != orders_.end()) {
                with_ladder(it->second.side, [&](auto& ladder) {
                    if (msg.qty != 0) {
                        ladder[it->second.price] -= msg.qty;
                        cleanup(ladder, it->second.price);
                        it->second.qty -= msg.qty;
                        if (it->second.qty <= 0) orders_.erase(it);
                    } else {
                        ladder.erase(it->second.price);
                        orders_.erase(it);
                    }
                });
                break;
            }
        }
        // Fallback: delete by price (Binance path)
        with_ladder(msg.side, [&](auto& ladder) { ladder.erase(msg.px); });
        break;

    case MessageType::Replace:
        handle_replace(msg);
        break;

    case MessageType::Execute:
        if (msg.order_id != 0) {
            auto it = orders_.find(msg.order_id);
            if (it != orders_.end()) {
                with_ladder(it->second.side, [&](auto& ladder) {
                    ladder[it->second.price] -= msg.qty;
                    cleanup(ladder, it->second.price);
                    it->second.qty -= msg.qty;
                    if (it->second.qty <= 0) orders_.erase(it);
                });
                break;
            }
        }
        // No order_id — fall through to trade-by-price
        [[fallthrough]];

    case MessageType::Trade:
        // L3 path: look up the resting order by order_id
        if (msg.order_id != 0) {
            auto it = orders_.find(msg.order_id);
            if (it != orders_.end()) {
                with_ladder(it->second.side, [&](auto& ladder) {
                    if (msg.px != 0) {
                        ladder[msg.px] -= msg.qty;
                        cleanup(ladder, msg.px);
                    }
                    it->second.qty -= msg.qty;
                    if (it->second.qty <= 0) orders_.erase(it);
                });
                break;
            }
        }
        // L2 path: trade reduces liquidity on the same side (backward compatible)
        with_ladder(msg.side, [&](auto& ladder) {
            if (msg.px != 0) {
                auto it = ladder.find(msg.px);
                if (it != ladder.end()) {
                    it->second -= msg.qty;
                    if (it->second <= 0) ladder.erase(it);
                }
            }
        });
        break;

    case MessageType::Snapshot:
        with_ladder(msg.side, [&](auto& ladder) { ladder[msg.px] = msg.qty; });
        if (msg.order_id != 0) {
            orders_[msg.order_id] = {msg.px, msg.qty, msg.side};
        }
        break;
    }
}

void Reconstructor::handle_replace(const BookMessage& msg) {
    auto it = orders_.find(msg.order_id);
    if (it == orders_.end()) return;

    auto with_ladder = [&](Side s, auto&& fn) -> void {
        if (s == Side::Buy) fn(bids_); else fn(asks_);
    };

    // 1. Remove old order's quantity from its price level
    with_ladder(it->second.side, [&](auto& ladder) {
        ladder[it->second.price] -= it->second.qty;
        if (ladder[it->second.price] <= 0) {
            ladder.erase(it->second.price);
        }
    });

    // 2. Remove old order from map
    orders_.erase(it);

    // 3. Insert new order into ladder
    with_ladder(msg.side, [&](auto& ladder) {
        ladder[msg.px] += msg.qty;
    });

    // 4. Record new order in map
    orders_[msg.new_order_id] = {msg.px, msg.qty, msg.side};
}

BookSnapshot Reconstructor::snapshot() const {
    BookSnapshot s;
    s.seq   = seq_;
    s.ts_us = seq_;
    if (!bids_.empty()) {
        s.bid = PriceLevel{bids_.begin()->first, bids_.begin()->second};
    }
    if (!asks_.empty()) {
        s.ask = PriceLevel{asks_.begin()->first, asks_.begin()->second};
    }
    return s;
}

Qty Reconstructor::volume_at(Price px, Side side) const noexcept {
    if (side == Side::Buy) {
        auto it = bids_.find(px);
        return it != bids_.end() ? it->second : 0;
    }
    auto it = asks_.find(px);
    return it != asks_.end() ? it->second : 0;
}

std::optional<OrderRef> Reconstructor::lookup(OrderId id) const noexcept {
    auto it = orders_.find(id);
    if (it != orders_.end()) return it->second;
    return std::nullopt;
}

} // namespace lob