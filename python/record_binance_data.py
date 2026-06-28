"""Record real Binance BTCUSDT depth data via WebSocket + REST snapshot."""

import sys
import json
import time
import asyncio
import argparse
from pathlib import Path
from collections import deque

import requests
import websockets
import polars as pl
import numpy as np


SCALE = 10_000
ONE_US = 1_000_000


def to_fixed(price_str: str) -> int:
    return int(round(float(price_str) * SCALE))


def to_qty(qty_str: str) -> int:
    return int(float(qty_str))


class OrderBook:
    def __init__(self):
        self.bids: dict[int, int] = {}
        self.asks: dict[int, int] = {}
        self.seq = 0
        self._ofi = 0

    def apply_snapshot(self, bids: list, asks: list):
        self.bids.clear()
        self.asks.clear()
        for p, q in bids:
            fp, fq = to_fixed(p), to_qty(q)
            if fq > 0:
                self.bids[fp] = fq
        for p, q in asks:
            fp, fq = to_fixed(p), to_qty(q)
            if fq > 0:
                self.asks[fp] = fq
        self._ofi = 0

    def apply_diff(self, bids: list, asks: list):
        for p, q in bids:
            fp, fq = to_fixed(p), to_qty(q)
            old_qty = self.bids.get(fp, 0)
            if fq > 0:
                self.bids[fp] = fq
            else:
                self.bids.pop(fp, None)
            delta = fq - old_qty
            if delta > 0:
                self._ofi += delta
        for p, q in asks:
            fp, fq = to_fixed(p), to_qty(q)
            old_qty = self.asks.get(fp, 0)
            if fq > 0:
                self.asks[fp] = fq
            else:
                self.asks.pop(fp, None)
            delta = fq - old_qty
            if delta > 0:
                self._ofi -= delta

    @property
    def best_bid(self) -> int:
        return max(self.bids.keys()) if self.bids else 0

    @property
    def best_ask(self) -> int:
        return min(self.asks.keys()) if self.asks else 0

    @property
    def best_bid_qty(self) -> int:
        return self.bids.get(self.best_bid, 0)

    @property
    def best_ask_qty(self) -> int:
        return self.asks.get(self.best_ask, 0)

    def midprice(self) -> float:
        bb, ba = self.best_bid, self.best_ask
        if bb == 0 or ba == 0:
            return float("nan")
        return (bb + ba) / (2 * SCALE)

    def spread(self) -> float:
        bb, ba = self.best_bid, self.best_ask
        if bb == 0 or ba == 0:
            return float("nan")
        return (ba - bb) / SCALE

    def microprice(self) -> float:
        bb, ba = self.best_bid, self.best_ask
        bq, aq = self.best_bid_qty, self.best_ask_qty
        if bb == 0 or ba == 0 or (bq + aq) == 0:
            return float("nan")
        return ((bq * ba / SCALE) + (aq * bb / SCALE)) / (bq + aq)

    def queue_imbalance(self) -> float:
        bq, aq = self.best_bid_qty, self.best_ask_qty
        total = bq + aq
        return bq / total if total > 0 else 0.5

    def ofi(self) -> int:
        return self._ofi

    def reset_ofi(self):
        self._ofi = 0


async def record(duration_sec: int, output: str, snapshot_interval: int = 1, limit: int = 100):
    out = Path(output)
    out.mkdir(parents=True, exist_ok=True)

    book = OrderBook()
    parquet_path = out / "btc_depth.parquet"

    snapshot_rows: list[dict] = []
    event_count = 0
    last_capture_seq = -snapshot_interval
    arrival_times: deque[int] = deque()
    cancel_times: deque[int] = deque()

    # ── Step 1: Fetch REST snapshot ────────────────────────────────────
    msg = f"Fetching REST snapshot (limit={limit})..."
    print(msg, flush=True)
    resp = requests.get(
        "https://api.binance.com/api/v3/depth",
        params={"symbol": "BTCUSDT", "limit": limit},
    )
    snap = resp.json()
    last_update_id = snap["lastUpdateId"]
    print(f"  Snapshot lastUpdateId={last_update_id}", flush=True)

    book.apply_snapshot(snap["bids"], snap["asks"])

    # ── Step 2: WebSocket — buffer + sync ───────────────────────────────
    print("Connecting WebSocket depth stream...", flush=True)
    async with websockets.connect("wss://stream.binance.com:9443/ws/btcusdt@depth@100ms") as ws:
        # Buffer events arriving while we fetch snapshot
        print("  Connected, buffering initial events...", flush=True)
        buffer = []
        for _ in range(20):
            try:
                msg = await asyncio.wait_for(ws.recv(), timeout=3.0)
                ev = json.loads(msg)
                if "U" in ev:
                    buffer.append(ev)
            except asyncio.TimeoutError:
                break

        print(f"  Buffered {len(buffer)} events", flush=True)

        # Apply events that follow the snapshot
        applied = 0
        for ev in buffer:
            if ev["U"] > last_update_id or (ev["U"] <= last_update_id + 1 and ev["u"] > last_update_id):
                book.apply_diff(ev["b"], ev["a"])
                applied += 1

        print(f"  Applied {applied}/{len(buffer)} buffered events", flush=True)

        # ── Step 3: Live record ─────────────────────────────────────────
        start_time = time.monotonic()
        print(f"Recording for {duration_sec}s...", flush=True)

        while time.monotonic() - start_time < duration_sec:
            try:
                msg = await asyncio.wait_for(ws.recv(), timeout=0.5)
            except asyncio.TimeoutError:
                continue

            ev = json.loads(msg)
            if "U" not in ev:
                continue

            event_count += 1
            ts_us = int(time.time() * ONE_US)

            # Count arrivals/cancels for rate tracking
            n_arrivals = 0
            n_cancels = 0
            for p, q in ev["b"] + ev["a"]:
                if float(q) > 0:
                    n_arrivals += 1
                else:
                    n_cancels += 1

            arrival_times.extend([ts_us] * n_arrivals)
            cancel_times.extend([ts_us] * n_cancels)

            cutoff = ts_us - ONE_US
            while arrival_times and arrival_times[0] < cutoff:
                arrival_times.popleft()
            while cancel_times and cancel_times[0] < cutoff:
                cancel_times.popleft()

            book.apply_diff(ev["b"], ev["a"])

            # Capture features at snapshot_interval
            if snapshot_interval == 0 or (event_count - last_capture_seq >= snapshot_interval):
                last_capture_seq = event_count
                bb, ba = book.best_bid, book.best_ask
                if bb == 0 or ba == 0:
                    continue

                row = {
                    "seq": event_count,
                    "ts_us": ts_us,
                    "best_bid": bb,
                    "best_bid_qty": book.best_bid_qty,
                    "best_ask": ba,
                    "best_ask_qty": book.best_ask_qty,
                    "midprice": book.midprice(),
                    "spread": book.spread(),
                    "microprice": book.microprice(),
                    "ofi": book.ofi(),
                    "queue_imbalance": book.queue_imbalance(),
                    "arrival_rate": len(arrival_times),
                    "cancel_rate": len(cancel_times),
                }
                snapshot_rows.append(row)
                book.reset_ofi()

            if event_count % 1000 == 0:
                elapsed = time.monotonic() - start_time
                print(f"  {event_count} events, {len(snapshot_rows)} snapshots, {elapsed:.0f}s", flush=True)

        elapsed_total = time.monotonic() - start_time

    # ── Step 4: Write Parquet ───────────────────────────────────────────
    print(f"\nDone. {event_count} events in {elapsed_total:.0f}s", flush=True)
    print(f"  Captured {len(snapshot_rows)} snapshot rows", flush=True)

    if snapshot_rows:
        df = pl.DataFrame(snapshot_rows)
        df = df.sort("ts_us")
        df.write_parquet(str(parquet_path), compression="zstd")
        print(f"  Wrote {parquet_path} ({len(df)} rows, {len(df.columns)} cols)", flush=True)
    else:
        print("  ERROR: No snapshot rows captured!", flush=True)

    return parquet_path


def main():
    parser = argparse.ArgumentParser(description="Record Binance BTCUSDT depth data")
    parser.add_argument("--duration", type=int, default=600)
    parser.add_argument("--output", type=str, default="data")
    parser.add_argument("--interval", type=int, default=1)
    parser.add_argument("--limit", type=int, default=100)
    args = parser.parse_args()

    result = asyncio.run(record(
        duration_sec=args.duration,
        output=args.output,
        snapshot_interval=args.interval,
        limit=args.limit,
    ))
    print(f"Recorded data: {result}", flush=True)


if __name__ == "__main__":
    main()
