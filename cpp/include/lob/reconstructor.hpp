#pragma once

#include "lob/types.hpp"
#include <map>
#include <unordered_map>
#include <cstdint>

namespace lob {

// ── PriceLevel ──────────────────────────────────────────────────────────────

struct PriceLevel {
    Price price  = 0;
    Qty   volume = 0;
};

// ── BookSnapshot ────────────────────────────────────────────────────────────

struct BookSnapshot {
    uint64_t    seq   = 0;
    uint64_t    ts_us = 0;
    PriceLevel  bid;
    PriceLevel  ask;
};

// ── Reconstructor ───────────────────────────────────────────────────────────

class Reconstructor {
public:
    Reconstructor() = default;

    void apply(const BookMessage& msg);

    [[nodiscard]] BookSnapshot snapshot() const;

    // Exposed for feature engine / queue model
    [[nodiscard]] const auto& bids() const noexcept { return bids_; }
    [[nodiscard]] const auto& asks() const noexcept { return asks_; }
    [[nodiscard]] const auto& orders() const noexcept { return orders_; }

    [[nodiscard]] Qty volume_at(Price px, Side side) const noexcept;
    [[nodiscard]] std::optional<OrderRef> lookup(OrderId id) const noexcept;

private:
    // bids_ sorted descending (best bid first)
    std::map<Price, Qty, std::greater<Price>> bids_;
    // asks_ sorted ascending (best ask first)
    std::map<Price, Qty, std::less<Price>>    asks_;

    // Per-order state for L3 book tracking
    std::unordered_map<OrderId, OrderRef>     orders_;

    uint64_t seq_ = 0;

    void handle_replace(const BookMessage& msg);
};

} // namespace lob