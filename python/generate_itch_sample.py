"""Generate a synthetic ITCH v5.x binary file for testing the replay pipeline.

Produces a realistic sequence of Add/Execute/Cancel/Delete/Trade/Replace messages
on a single symbol with millisecond-level timing.
"""

import struct
import os

# ── Helpers ─────────────────────────────────────────────────────────────────

def be16(v): return struct.pack(">H", v)
def be32(v): return struct.pack(">I", v)
def be64(v): return struct.pack(">Q", v)

def be48(v):
    """6-byte big-endian uint48."""
    return struct.pack(">Q", v)[2:]

def make_header(msg_type, tracking, ts_ns):
    """Build the 11-byte ITCH standard header."""
    return struct.pack("<B", ord(msg_type)) + be16(0) + be16(tracking) + be48(ts_ns)

# ── Message builders (all fields big-endian except msg_type) ───────────────

def system_event(ts_ns, tracking=1, event_code='O'):
    """S - System Event (12 bytes). 'O' = start of hours."""
    return make_header('S', tracking, ts_ns) + event_code.encode()

def add_order(ts_ns, order_ref, side, shares, stock, price, tracking=1):
    """A - Add Order (40 bytes)"""
    return (make_header('A', tracking, ts_ns)
            + be64(order_ref)
            + side.encode()             # 'B' or 'S'
            + be32(shares)
            + stock.ljust(8).encode()
            + be32(price))

def order_executed(ts_ns, order_ref, shares, match, tracking=1):
    """E - Order Executed (31 bytes)"""
    return (make_header('E', tracking, ts_ns)
            + be64(order_ref)
            + be32(shares)
            + be64(match))

def order_cancel(ts_ns, order_ref, canceled_shares, tracking=1):
    """X - Order Cancel (23 bytes)"""
    return (make_header('X', tracking, ts_ns)
            + be64(order_ref)
            + be32(canceled_shares))

def order_delete(ts_ns, order_ref, tracking=1):
    """D - Order Delete (19 bytes)"""
    return make_header('D', tracking, ts_ns) + be64(order_ref)

def order_replace(ts_ns, orig_ref, new_ref, shares, price, tracking=1):
    """U - Order Replace (37 bytes)"""
    return (make_header('U', tracking, ts_ns)
            + be64(orig_ref)
            + be64(new_ref)
            + be32(shares)
            + be32(price))

def trade(ts_ns, order_ref, side, shares, stock, price, match, tracking=1):
    """P - Trade (44 bytes)"""
    return (make_header('P', tracking, ts_ns)
            + be64(order_ref)
            + side.encode()
            + be32(shares)
            + stock.ljust(8).encode()
            + be32(price)
            + be64(match))

def cross_trade(ts_ns, shares, stock, price, match, cross_type='O', tracking=1):
    """Q - Cross Trade (36 bytes). 'O' = opening cross."""
    return (make_header('Q', tracking, ts_ns)
            + be32(shares)
            + stock.ljust(8).encode()
            + be32(price)
            + be64(match)
            + cross_type.encode())

# ── Scenario: realistic 5-second market simulation ─────────────────────────

def generate_scenario(out_path: str, symbol: str = "AAPL    ", price_scale: int = 10000):
    """
    Generate a 5-second ITCH binary with ~200 events.

    Scenario:
    1. System event (start of day)
    2. Add 5 bid orders at descending prices (wide spread)
    3. Add 5 ask orders at ascending prices
    4. Add 3 more aggressive bids (inside spread)
    5. Execute 2 bids partially
    6. Cancel 1 bid
    7. Add more asks
    8. Replace a bid (improve price)
    9. Trade at ask side
    10. Execute remaining bids
    11. Cross trade & delete
    12-30. More random orders
    31-50. Multiple executions
    51-70. Cancellations
    71-100. Trades and crosses
    101-120. Replace orders
    """
    msgs = bytearray()
    tracking = 1
    ts = 0          # nanoseconds from midnight
    match = 1
    next_ref = 1

    def emit(data):
        nonlocal tracking, ts, match, next_ref
        msgs.extend(data)
        tracking += 1

    base_ts = 9 * 3600 * 1_000_000_000  # 9:00 AM in ns

    # 1. System event
    emit(system_event(base_ts + ts, tracking, 'O'))
    ts += 1_000_000  # 1ms

    # 2. Add 5 bids (150.00 down to 149.00 in 0.25 increments)
    bid_prices = [1500000, 1497500, 1495000, 1492500, 1490000]
    bid_refs = []
    for px in bid_prices:
        ref = next_ref; next_ref += 1
        bid_refs.append(ref)
        emit(add_order(base_ts + ts, ref, 'B', 1000, symbol, px, tracking))
        ts += 500_000  # 0.5ms

    # 3. Add 5 asks (151.00 up to 152.00 in 0.25 increments)
    ask_prices = [1510000, 1512500, 1515000, 1517500, 1520000]
    ask_refs = []
    for px in ask_prices:
        ref = next_ref; next_ref += 1
        ask_refs.append(ref)
        emit(add_order(base_ts + ts, ref, 'S', 800, symbol, px, tracking))
        ts += 500_000

    # 4. Add 3 aggressive bids (inside the spread)
    for px in [1500500, 1501000, 1501500]:
        ref = next_ref; next_ref += 1
        bid_refs.append(ref)
        emit(add_order(base_ts + ts, ref, 'B', 500, symbol, px, tracking))
        ts += 300_000

    # 5. Execute 2 bids partially
    for i in range(2):
        emit(order_executed(base_ts + ts, bid_refs[i], 300, match, tracking))
        match += 1
        ts += 200_000

    # 6. Cancel 1 bid (the highest aggressive)
    emit(order_cancel(base_ts + ts, bid_refs[-1], 500, tracking))
    ts += 200_000

    # 7. Add more asks
    ref = next_ref; next_ref += 1
    emit(add_order(base_ts + ts, ref, 'S', 1200, symbol, 1505000, tracking))
    ask_refs.append(ref)
    ts += 300_000

    # 8. Replace a bid (improve price from 1495000 to 1496000)
    new_ref = next_ref; next_ref += 1
    emit(order_replace(base_ts + ts, bid_refs[2], new_ref, 800, 1496000, tracking))
    bid_refs[2] = new_ref
    ts += 400_000

    # 9. Trade at ask
    emit(trade(base_ts + ts, ask_refs[1], 'S', 400, symbol, ask_prices[1], match, tracking))
    match += 1
    ts += 500_000

    # 10. Delete an ask
    emit(order_delete(base_ts + ts, ask_refs[-2], tracking))
    ts += 300_000

    # 11. Cross trade
    emit(cross_trade(base_ts + ts, 2000, symbol, 1500000, match, 'O', tracking))
    match += 1
    ts += 400_000

    # 12-30. Additional events to reach ~200 (more orders)
    for i in range(19):
        ref = next_ref; next_ref += 1
        if i % 2 == 0:
            px = 1495000 - i // 2
            emit(add_order(base_ts + ts, ref, 'B', 300, symbol, px, tracking))
            bid_refs.append(ref)
        else:
            px = 1515000 + i // 2
            emit(add_order(base_ts + ts, ref, 'S', 250, symbol, px, tracking))
            ask_refs.append(ref)
        ts += 150_000

    # 31-50. Multiple executions
    for i in range(10):
        if i < len(bid_refs):
            emit(order_executed(base_ts + ts, bid_refs[i], 150, match, tracking))
            match += 1
            ts += 200_000
        if i < len(ask_refs):
            emit(order_executed(base_ts + ts, ask_refs[i], 100, match, tracking))
            match += 1
            ts += 200_000

    # 51-70. Cancellations
    for i in range(10, 20):
        if i < len(bid_refs):
            emit(order_cancel(base_ts + ts, bid_refs[i], 50, tracking))
            ts += 150_000

    # 71-100. Trades
    for i in range(5):
        if i < len(ask_refs):
            emit(trade(base_ts + ts, ask_refs[i], 'S', 300, symbol, 1520000, match, tracking))
            match += 1
            ts += 300_000
        emit(cross_trade(base_ts + ts, 500, symbol, 1505000, match, 'C', tracking))
        match += 1
        ts += 400_000

    # 101-120. Replace orders
    for i in range(5, 10):
        if i < len(bid_refs):
            new_ref = next_ref; next_ref += 1
            emit(order_replace(base_ts + ts, bid_refs[i], new_ref, 400, 1495000, tracking))
            bid_refs[i] = new_ref
            ts += 200_000

    # Final cleanup - delete some random orders
    del_refs = []
    for i in range(len(bid_refs)):
        if i % 3 == 0:
            del_refs.append(bid_refs[i])
    for i in range(len(ask_refs)):
        if i % 3 == 0:
            del_refs.append(ask_refs[i])

    for ref in del_refs:
        emit(order_delete(base_ts + ts, ref, tracking))
        ts += 100_000

    # Write file
    with open(out_path, "wb") as f:
        f.write(msgs)

    n = msgs.__len__()
    print(f"Wrote {n} bytes ({tracking - 1} messages) to {out_path}")
    print(f"  Time span: {ts / 1e9:.3f} seconds")
    print(f"  Total events: {tracking - 1}")
    return n


if __name__ == "__main__":
    out = os.path.join(os.path.dirname(__file__) or ".", "..", "data", "sample_itch.bin")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    generate_scenario(out)