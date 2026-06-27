#pragma once

#include "lob/types.hpp"
#include <string>
#include <vector>

namespace lob {

// ── ParseResult ─────────────────────────────────────────────────────────────

struct ParseResult {
    uint64_t              first_update_id = 0;
    uint64_t              final_update_id = 0;
    std::vector<BookMessage> messages;
};

// ── BinanceParser ───────────────────────────────────────────────────────────

class BinanceParser {
public:
    // Parse one depthUpdate event JSON → ordered sequence of BookMessages.
    static ParseResult parse_depth_update(const std::string& json);

    // Parse REST snapshot JSON → Snapshot-type BookMessages for each level.
    static std::vector<BookMessage> parse_snapshot(const std::string& json);
};

} // namespace lob
