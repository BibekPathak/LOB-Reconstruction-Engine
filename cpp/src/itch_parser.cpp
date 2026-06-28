#include "lob/itch_parser.hpp"

namespace lob {

void ItchParser::reset() noexcept {
    expected_ = 0;
    has_gap_  = false;
    last_gap_ = {};
}

std::optional<BookMessage> ItchParser::parse_one(
    const uint8_t* data, size_t len, size_t& offset)
{
    if (offset >= len) return std::nullopt;

    char type = static_cast<char>(data[offset]);
    int sz = itch_msg_size(type);
    if (sz == 0 || offset + static_cast<size_t>(sz) > len) {
        // Unknown or truncated — skip one byte and continue
        offset += 1;
        return std::nullopt;
    }

    // Sequence gap detection
    if (sz >= static_cast<int>(sizeof(ItchHeader))) {
        ItchHeader hdr;
        std::memcpy(&hdr, data + offset, sizeof(ItchHeader));
        uint16_t track = hdr.track();
        if (expected_ != 0 && track != static_cast<uint16_t>(expected_ + 1)) {
            has_gap_ = true;
            last_gap_ = {static_cast<uint16_t>(expected_ + 1), track};
        }
        expected_ = track;
    }

    uint64_t ts_us = 0;
    if (sz >= static_cast<int>(sizeof(ItchHeader))) {
        ItchHeader hdr;
        std::memcpy(&hdr, data + offset, sizeof(ItchHeader));
        ts_us = hdr.ts_ns() / 1000;  // nanoseconds → microseconds
    }

    auto make_msg = [&](MessageType mt, uint64_t oid, Side s, Price px, Qty qty, uint64_t noid = 0) {
        BookMessage m;
        m.seq = ts_us;
        m.ts_us = ts_us;
        m.type = mt;
        m.order_id = oid;
        m.new_order_id = noid;
        m.px = px;
        m.qty = qty;
        m.side = s;
        return m;
    };

    switch (type) {

    case 'A': {
        ItchAddOrder msg;
        std::memcpy(&msg, data + offset, sizeof(msg));
        Side s = msg.buy_sell_indicator == 'B' ? Side::Buy : Side::Sell;
        offset += sizeof(msg);
        return make_msg(MessageType::Add, msg.order_id(), s,
                        static_cast<Price>(msg.px()),
                        static_cast<Qty>(msg.share_qty()));
    }

    case 'F': {
        ItchAddOrderMpid msg;
        std::memcpy(&msg, data + offset, sizeof(msg));
        Side s = msg.buy_sell_indicator == 'B' ? Side::Buy : Side::Sell;
        offset += sizeof(msg);
        return make_msg(MessageType::Add, msg.order_id(), s,
                        static_cast<Price>(msg.px()),
                        static_cast<Qty>(msg.share_qty()));
    }

    case 'E': {
        ItchOrderExecuted msg;
        std::memcpy(&msg, data + offset, sizeof(msg));
        offset += sizeof(msg);
        // Price/side resolved via order_id lookup in Reconstructor
        return make_msg(MessageType::Execute, msg.order_id(), Side::Buy, 0,
                        static_cast<Qty>(msg.shares()));
    }

    case 'C': {
        ItchOrderExecPrice msg;
        std::memcpy(&msg, data + offset, sizeof(msg));
        offset += sizeof(msg);
        // Price provided directly — can be treated as Trade
        return make_msg(MessageType::Trade, msg.order_id(), Side::Buy,
                        static_cast<Price>(msg.px()),
                        static_cast<Qty>(msg.shares()));
    }

    case 'X': {
        ItchOrderCancel msg;
        std::memcpy(&msg, data + offset, sizeof(msg));
        offset += sizeof(msg);
        return make_msg(MessageType::Delete, msg.order_id(), Side::Buy, 0,
                        static_cast<Qty>(msg.shares()));
    }

    case 'D': {
        ItchOrderDelete msg;
        std::memcpy(&msg, data + offset, sizeof(msg));
        offset += sizeof(msg);
        return make_msg(MessageType::Delete, msg.order_id(), Side::Buy, 0, 0);
    }

    case 'U': {
        ItchOrderReplace msg;
        std::memcpy(&msg, data + offset, sizeof(msg));
        offset += sizeof(msg);
        BookMessage m;
        m.seq = ts_us;
        m.ts_us = ts_us;
        m.type = MessageType::Replace;
        m.order_id = msg.orig_order_id();
        m.new_order_id = msg.new_order_id();
        m.px = static_cast<Price>(msg.px());
        m.qty = static_cast<Qty>(msg.share_qty());
        // side is looked up from the old order in Reconstructor
        return m;
    }

    case 'P': {
        ItchTrade msg;
        std::memcpy(&msg, data + offset, sizeof(msg));
        Side s = msg.buy_sell_indicator == 'B' ? Side::Buy : Side::Sell;
        offset += sizeof(msg);
        return make_msg(MessageType::Trade, msg.order_id(), s,
                        static_cast<Price>(msg.px()),
                        static_cast<Qty>(msg.share_qty()));
    }

    case 'Q': {
        ItchCrossTrade msg;
        std::memcpy(&msg, data + offset, sizeof(msg));
        offset += sizeof(msg);
        // Cross trades have no side — default to Buy
        return make_msg(MessageType::Trade, 0, Side::Buy,
                        static_cast<Price>(msg.px()),
                        static_cast<Qty>(msg.share_qty()));
    }

    case 'S':
        // System event — no book change
        offset += sizeof(ItchSystemEvent);
        return std::nullopt;

    default:
        // Unknown — skip one byte
        offset += 1;
        return std::nullopt;
    }
}

ItchParseResult ItchParser::parse(const uint8_t* data, size_t len) {
    ItchParseResult result;
    size_t offset = 0;

    while (offset < len) {
        auto msg = parse_one(data, len, offset);
        if (msg.has_value()) {
            if (result.messages.empty()) {
                result.first_track = expected_;
            }
            result.messages.push_back(std::move(*msg));
        }
    }

    result.last_track = expected_;
    return result;
}

} // namespace lob