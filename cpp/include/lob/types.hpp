#pragma once

#include <cstdint>
#include <compare>
#include <optional>

namespace lob {

// ── Fixed-point price ───────────────────────────────────────────────────────

using Price = int64_t;
using Qty   = int64_t;
using OrderId = uint64_t;

inline constexpr Price SCALE = 10'000; // 1e-4

inline Price to_price(double x) noexcept {
    return static_cast<Price>(x * SCALE);
}

inline double to_double(Price p) noexcept {
    return static_cast<double>(p) / SCALE;
}

// ── Side ────────────────────────────────────────────────────────────────────

enum class Side : uint8_t { Buy, Sell };

inline Side opposite(Side s) noexcept {
    return s == Side::Buy ? Side::Sell : Side::Buy;
}

// ── MessageType ─────────────────────────────────────────────────────────────

enum class MessageType : uint8_t {
    Snapshot,
    Add,
    Modify,
    Delete,
    Replace,
    Execute,
    Trade
};

// ── OrderRef ────────────────────────────────────────────────────────────────

struct OrderRef {
    Price price = 0;
    Qty   qty   = 0;
    Side  side  = Side::Buy;
};

// ── BookMessage ─────────────────────────────────────────────────────────────

struct BookMessage {
    uint64_t    seq          = 0;
    uint64_t    ts_us        = 0;
    MessageType type         = MessageType::Add;
    uint64_t    order_id     = 0;
    uint64_t    new_order_id = 0;
    Price       px           = 0;
    Qty         qty          = 0;
    Side        side         = Side::Buy;

    friend auto operator<=>(const BookMessage&, const BookMessage&) = default;
};

} // namespace lob
