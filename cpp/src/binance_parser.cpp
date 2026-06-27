#include "lob/binance_parser.hpp"
#include <nlohmann/json.hpp>
#include <charconv>

namespace lob {

using json = nlohmann::json;

// ── Helpers ─────────────────────────────────────────────────────────────────

static Price parse_price(const std::string& s) {
    // Binance gives decimal strings like "100.50"
    // Convert to fixed-point: multiply by SCALE, rounding to nearest integer.
    // We use string parsing to avoid floating-point inaccuracies.
    double val = std::stod(s);
    return to_price(val);
}

static Qty parse_qty(const std::string& s) {
    double val = std::stod(s);
    return static_cast<Qty>(std::llround(val));
}

static MessageType classify(double qty_double) {
    if (qty_double == 0.0) return MessageType::Delete;
    return MessageType::Modify;  // caller fixes up to Add if new
}

// ── Level iterator ──────────────────────────────────────────────────────────

static void parse_side(
    const json& entries,
    Side side,
    uint64_t seq,
    uint64_t ts_us,
    std::vector<BookMessage>& out)
{
    for (const auto& item : entries) {
        const auto& price_str = item[0].get_ref<const std::string&>();
        const auto& qty_str   = item[1].get_ref<const std::string&>();

        Price px = parse_price(price_str);
        Qty   qty  = parse_qty(qty_str);
        auto msg = BookMessage{
            .seq      = seq,
            .ts_us    = ts_us,
            .order_id = static_cast<uint64_t>(px), // use price as stable ID
            .px       = px,
            .qty      = qty,
            .side     = side,
        };

        if (qty == 0) {
            msg.type = MessageType::Delete;
        } else {
            // Normalizer will upgrade first occurrence of a price to Add
            msg.type = MessageType::Modify;
        }

        out.push_back(msg);
    }
}

// ── Parse depthUpdate ───────────────────────────────────────────────────────

ParseResult BinanceParser::parse_depth_update(const std::string& json_str) {
    ParseResult result;
    auto j = json::parse(json_str);

    uint64_t event_ts = j.value("E", 0ULL) * 1000;  // ms → μs
    uint64_t U = j.value("U", 0ULL);
    uint64_t u = j.value("u", 0ULL);

    result.first_update_id = U;
    result.final_update_id = u;

    // Use u as the stable seq for all messages in this event
    uint64_t seq = u;

    if (j.contains("b")) {
        parse_side(j["b"], Side::Buy,  seq, event_ts, result.messages);
    }
    if (j.contains("a")) {
        parse_side(j["a"], Side::Sell, seq, event_ts, result.messages);
    }

    return result;
}

// ── Parse snapshot ──────────────────────────────────────────────────────────

std::vector<BookMessage> BinanceParser::parse_snapshot(const std::string& json_str) {
    auto j = json::parse(json_str);

    uint64_t last_update_id = j.value("lastUpdateId", 0ULL);
    uint64_t ts_us = 0;  // snapshots don't carry a timestamp

    std::vector<BookMessage> out;

    auto parse_side_snap = [&](const json& entries, Side side) {
        for (const auto& item : entries) {
            Price px = parse_price(item[0].get_ref<const std::string&>());
            Qty   qty  = parse_qty(item[1].get_ref<const std::string&>());
            if (qty == 0) continue;

            out.push_back(BookMessage{
                .seq      = last_update_id,
                .ts_us    = ts_us,
                .type     = MessageType::Snapshot,
                .order_id = static_cast<uint64_t>(px),
                .px       = px,
                .qty      = qty,
                .side     = side,
            });
        }
    };

    if (j.contains("bids"))  parse_side_snap(j["bids"],  Side::Buy);
    if (j.contains("asks"))  parse_side_snap(j["asks"],  Side::Sell);

    return out;
}

} // namespace lob
