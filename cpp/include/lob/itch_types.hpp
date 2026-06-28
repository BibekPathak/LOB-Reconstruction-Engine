#pragma once

#include "lob/types.hpp"
#include <cstdint>
#include <cstring>
#include <optional>

namespace lob {

// ── Big-endian helpers ──────────────────────────────────────────────────────

inline uint64_t read_be48(const uint8_t* p) noexcept {
    return (uint64_t(p[0]) << 40) | (uint64_t(p[1]) << 32)
         | (uint64_t(p[2]) << 24) | (uint64_t(p[3]) << 16)
         | (uint64_t(p[4]) << 8)  | uint64_t(p[5]);
}

inline uint64_t read_be64(const uint8_t* p) noexcept {
    return (uint64_t(p[0]) << 56) | (uint64_t(p[1]) << 48)
         | (uint64_t(p[2]) << 40) | (uint64_t(p[3]) << 32)
         | (uint64_t(p[4]) << 24) | (uint64_t(p[5]) << 16)
         | (uint64_t(p[6]) << 8)  | uint64_t(p[7]);
}

inline uint32_t read_be32(const uint8_t* p) noexcept {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16)
         | (uint32_t(p[2]) << 8)  | uint32_t(p[3]);
}

inline uint16_t read_be16(const uint8_t* p) noexcept {
    return (uint16_t(p[0]) << 8) | uint16_t(p[1]);
}

// ── ITCH v5.x packed message structs ────────────────────────────────────────
// All multi-byte fields are big-endian; use read_beXX() helpers.
// Standard header (11 bytes) present in every message:
//   msg_type(1) + stock_locate(2) + tracking_number(2) + timestamp(6)

#pragma pack(push, 1)

struct ItchHeader {
    char     msg_type;
    uint8_t  stock_locate[2];
    uint8_t  tracking_number[2];
    uint8_t  timestamp[6];          // nanoseconds since midnight, uint48 BE

    uint64_t ts_ns()  const noexcept { return read_be48(timestamp); }
    uint16_t track()  const noexcept { return read_be16(tracking_number); }
};

// S – System Event (12 bytes)
struct ItchSystemEvent {
    ItchHeader hdr;
    char       event_code;
};

// A – Add Order (40 bytes)
struct ItchAddOrder {
    ItchHeader hdr;
    uint8_t    order_ref[8];        // uint64 BE
    char       buy_sell_indicator;
    uint8_t    shares[4];           // uint32 BE
    char       stock[8];
    uint8_t    price[4];            // uint32 BE, 4 decimal places

    uint64_t   order_id() const noexcept { return read_be64(order_ref); }
    uint32_t   share_qty() const noexcept { return read_be32(shares); }
    uint32_t   px()      const noexcept { return read_be32(price); }
};

// F – Add Order with MPID (44 bytes)
struct ItchAddOrderMpid {
    ItchHeader hdr;
    uint8_t    order_ref[8];
    char       buy_sell_indicator;
    uint8_t    shares[4];
    char       stock[8];
    uint8_t    price[4];
    char       attribution[4];

    uint64_t   order_id() const noexcept { return read_be64(order_ref); }
    uint32_t   share_qty() const noexcept { return read_be32(shares); }
    uint32_t   px()      const noexcept { return read_be32(price); }
};

// E – Order Executed (31 bytes)
struct ItchOrderExecuted {
    ItchHeader hdr;
    uint8_t    order_ref[8];
    uint8_t    executed_shares[4];   // uint32 BE
    uint8_t    match_number[8];      // uint64 BE

    uint64_t   order_id() const noexcept { return read_be64(order_ref); }
    uint32_t   shares()  const noexcept { return read_be32(executed_shares); }
};

// C – Order Executed with Price (36 bytes)
struct ItchOrderExecPrice {
    ItchHeader hdr;
    uint8_t    order_ref[8];
    uint8_t    executed_shares[4];
    uint8_t    match_number[8];
    char       printable;
    uint8_t    price[4];

    uint64_t   order_id() const noexcept { return read_be64(order_ref); }
    uint32_t   shares()  const noexcept { return read_be32(executed_shares); }
    uint32_t   px()      const noexcept { return read_be32(price); }
};

// X – Order Cancel (23 bytes)
struct ItchOrderCancel {
    ItchHeader hdr;
    uint8_t    order_ref[8];
    uint8_t    canceled_shares[4];

    uint64_t   order_id() const noexcept { return read_be64(order_ref); }
    uint32_t   shares()  const noexcept { return read_be32(canceled_shares); }
};

// D – Order Delete (19 bytes)
struct ItchOrderDelete {
    ItchHeader hdr;
    uint8_t    order_ref[8];

    uint64_t   order_id() const noexcept { return read_be64(order_ref); }
};

// U – Order Replace (37 bytes)
struct ItchOrderReplace {
    ItchHeader hdr;
    uint8_t    original_order_ref[8];
    uint8_t    new_order_ref[8];
    uint8_t    shares[4];
    uint8_t    price[4];

    uint64_t   orig_order_id() const noexcept { return read_be64(original_order_ref); }
    uint64_t   new_order_id()  const noexcept { return read_be64(new_order_ref); }
    uint32_t   share_qty()     const noexcept { return read_be32(shares); }
    uint32_t   px()            const noexcept { return read_be32(price); }
};

// P – Trade (44 bytes)
struct ItchTrade {
    ItchHeader hdr;
    uint8_t    order_ref[8];
    char       buy_sell_indicator;
    uint8_t    shares[4];
    char       stock[8];
    uint8_t    price[4];
    uint8_t    match_number[8];

    uint64_t   order_id() const noexcept { return read_be64(order_ref); }
    uint32_t   share_qty() const noexcept { return read_be32(shares); }
    uint32_t   px()      const noexcept { return read_be32(price); }
};

// Q – Cross Trade (36 bytes)
struct ItchCrossTrade {
    ItchHeader hdr;
    uint8_t    shares[4];
    char       stock[8];
    uint8_t    price[4];
    uint8_t    match_number[8];
    char       cross_type;

    uint32_t   share_qty() const noexcept { return read_be32(shares); }
    uint32_t   px()      const noexcept { return read_be32(price); }
};

#pragma pack(pop)

// ── Size lookup table ───────────────────────────────────────────────────────

inline constexpr int itch_msg_size(char type) noexcept {
    switch (type) {
        case 'S': return sizeof(ItchSystemEvent);
        case 'A': return sizeof(ItchAddOrder);
        case 'F': return sizeof(ItchAddOrderMpid);
        case 'E': return sizeof(ItchOrderExecuted);
        case 'C': return sizeof(ItchOrderExecPrice);
        case 'X': return sizeof(ItchOrderCancel);
        case 'D': return sizeof(ItchOrderDelete);
        case 'U': return sizeof(ItchOrderReplace);
        case 'P': return sizeof(ItchTrade);
        case 'Q': return sizeof(ItchCrossTrade);
        default:  return 0;
    }
}

} // namespace lob