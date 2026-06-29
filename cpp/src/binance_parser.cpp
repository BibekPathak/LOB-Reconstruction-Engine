#include "lob/binance_parser.hpp"
#include <simdjson.h>
#include <cmath>
#include <charconv>
#include <string_view>

namespace lob {

// ── Helpers ─────────────────────────────────────────────────────────────────

static Price parse_price(std::string_view s) {
    double val;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val);
    if (ec != std::errc()) return 0;
    return to_price(val);
}

static Qty parse_qty(std::string_view s) {
    double val;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val);
    if (ec != std::errc()) return 0;
    return static_cast<Qty>(std::llround(val));
}

// ── Level iterator ──────────────────────────────────────────────────────────

static void parse_side(
    simdjson::ondemand::array entries,
    Side side,
    uint64_t seq,
    uint64_t ts_us,
    std::vector<BookMessage>& out)
{
    for (auto item : entries) {
        simdjson::ondemand::array arr;
        if (item.get_array().get(arr)) continue;

        std::string_view price_str;
        std::string_view qty_str;
        if (arr.at(0).get_string().get(price_str)) continue;
        if (arr.at(1).get_string().get(qty_str)) continue;

        Price px = parse_price(price_str);
        Qty   qty = parse_qty(qty_str);

        BookMessage msg;
        msg.seq      = seq;
        msg.ts_us    = ts_us;
        msg.order_id = static_cast<uint64_t>(px);
        msg.px       = px;
        msg.qty      = qty;
        msg.side     = side;

        if (qty == 0) {
            msg.type = MessageType::Delete;
        } else {
            msg.type = MessageType::Modify;
        }

        out.push_back(msg);
    }
}

// ── Parse depthUpdate ───────────────────────────────────────────────────────

ParseResult BinanceParser::parse_depth_update(const std::string& json_str) {
    ParseResult result;
    simdjson::padded_string padded(json_str);
    simdjson::ondemand::parser parser;
    auto doc = parser.iterate(padded);

    uint64_t event_ts = 0;
    if (doc["E"].get_uint64().get(event_ts) != simdjson::SUCCESS) {
        event_ts = 0;
    }
    event_ts *= 1000;  // ms → μs

    uint64_t U = 0;
    doc["U"].get_uint64().get(U);

    uint64_t u = 0;
    doc["u"].get_uint64().get(u);

    result.first_update_id = U;
    result.final_update_id = u;
    uint64_t seq = u;

    simdjson::ondemand::array bids;
    if (doc["b"].get_array().get(bids) == simdjson::SUCCESS) {
        parse_side(bids, Side::Buy, seq, event_ts, result.messages);
    }

    simdjson::ondemand::array asks;
    if (doc["a"].get_array().get(asks) == simdjson::SUCCESS) {
        parse_side(asks, Side::Sell, seq, event_ts, result.messages);
    }

    return result;
}

// ── Parse snapshot ──────────────────────────────────────────────────────────

std::vector<BookMessage> BinanceParser::parse_snapshot(const std::string& json_str) {
    simdjson::padded_string padded(json_str);
    simdjson::ondemand::parser parser;
    auto doc = parser.iterate(padded);

    uint64_t last_update_id = 0;
    doc["lastUpdateId"].get_uint64().get(last_update_id);
    uint64_t ts_us = 0;

    std::vector<BookMessage> out;

    auto parse_side_snap = [&](simdjson::ondemand::array entries, Side side) {
        for (auto item : entries) {
            simdjson::ondemand::array arr;
            if (item.get_array().get(arr)) continue;

            std::string_view price_str;
            std::string_view qty_str;
            if (arr.at(0).get_string().get(price_str)) continue;
            if (arr.at(1).get_string().get(qty_str)) continue;

            Price px = parse_price(price_str);
            Qty   qty = parse_qty(qty_str);
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

    simdjson::ondemand::array bids;
    if (doc["bids"].get_array().get(bids) == simdjson::SUCCESS) {
        parse_side_snap(bids, Side::Buy);
    }

    simdjson::ondemand::array asks;
    if (doc["asks"].get_array().get(asks) == simdjson::SUCCESS) {
        parse_side_snap(asks, Side::Sell);
    }

    return out;
}

} // namespace lob
