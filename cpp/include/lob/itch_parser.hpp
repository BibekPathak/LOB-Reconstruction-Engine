#pragma once

#include "lob/itch_types.hpp"
#include "lob/types.hpp"
#include <vector>

namespace lob {

// ── ItchParseResult ─────────────────────────────────────────────────────────

struct ItchParseResult {
    uint64_t              first_track = 0;
    uint64_t              last_track  = 0;
    std::vector<BookMessage> messages;
};

// ── ItchSeqGap ──────────────────────────────────────────────────────────────

struct ItchSeqGap {
    uint16_t expected;
    uint16_t actual;
};

// ── ItchParser ──────────────────────────────────────────────────────────────

class ItchParser {
public:
    // Parse a contiguous buffer of ITCH binary messages.
    // Returns converted BookMessages and tracks sequence numbers.
    ItchParseResult parse(const uint8_t* data, size_t len);

    // Parse a single message starting at data[offset].
    // Returns nullopt for unknown/ignored message types.
    // Advances offset by the message size on success.
    std::optional<BookMessage> parse_one(const uint8_t* data, size_t len, size_t& offset);

    // Sequence gap accessors
    [[nodiscard]] uint16_t  expected_track() const noexcept { return expected_; }
    [[nodiscard]] bool      has_gap()        const noexcept { return has_gap_; }
    [[nodiscard]] ItchSeqGap last_gap()      const noexcept { return last_gap_; }

    // Reset state for a new batch/file
    void reset() noexcept;

private:
    uint16_t    expected_ = 0;
    bool        has_gap_  = false;
    ItchSeqGap  last_gap_{};
};

} // namespace lob