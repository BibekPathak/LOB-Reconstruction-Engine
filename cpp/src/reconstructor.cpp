#include "lob/reconstructor.hpp"

namespace lob {

void Reconstructor::apply(const BookMessage& msg) {
    seq_ = msg.ts_us;

    auto handle = [&](auto& book) {
        switch (msg.type) {

        case MessageType::Add:
            if (msg.qty > 0) {
                book[msg.px] = msg.qty;
            }
            break;

        case MessageType::Modify:
            if (msg.qty > 0) {
                book[msg.px] = msg.qty;
            } else {
                book.erase(msg.px);
            }
            break;

        case MessageType::Delete:
            book.erase(msg.px);
            break;

        case MessageType::Trade:
            if (msg.qty > 0) {
                auto it = book.find(msg.px);
                if (it != book.end()) {
                    it->second -= msg.qty;
                    if (it->second <= 0) {
                        book.erase(it);
                    }
                }
            }
            break;

        case MessageType::Snapshot:
            book[msg.px] = msg.qty;
            break;

        default:
            break;
        }
    };

    if (msg.side == Side::Buy) {
        handle(bids_);
    } else {
        handle(asks_);
    }
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

} // namespace lob
