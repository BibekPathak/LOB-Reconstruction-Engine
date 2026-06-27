#pragma once

#include "lob/types.hpp"
#include "lob/binance_parser.hpp"
#include <unordered_set>
#include <vector>

namespace lob {

// Normalizer converts exchange-specific messages into canonical BookMessages.
// For Binance: upgrades first occurrence of a price from Modify → Add.

class Normalizer {
public:
    // Process a ParseResult from the Binance parser
    std::vector<BookMessage> process(const ParseResult& result);

    // Process a snapshot directly
    std::vector<BookMessage> process_snapshot(const std::vector<BookMessage>& snapshot_msgs);

    void reset();

private:
    // Tracks which (side, price) pairs we've already seen.
    // Key: side << 63 | price   (ensures uniqueness across sides)
    using SeenKey = uint64_t;
    static SeenKey key(Side side, Price px) {
        uint64_t s = (side == Side::Sell) ? (1ULL << 63) : 0;
        return s | static_cast<uint64_t>(px);
    }
    std::unordered_set<SeenKey> seen_;
};

} // namespace lob
