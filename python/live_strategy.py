"""Live Binance BTCUSDT strategy — real-time LOB features + ML prediction + PnL."""

import sys
import json
import time
import math
import asyncio
import argparse
from pathlib import Path
from collections import deque

import requests
import websockets
import polars as pl
import numpy as np
import joblib

# ── Models: load best 12-feat model (label_5) ─────────────────────────────

MODEL_DIR = Path(__file__).resolve().parent.parent / "models" / "v2"
N_FEATURES = 12
FEATURE_NAMES = [
    "midprice", "spread", "microprice", "ofi",
    "queue_imbalance", "arrival_rate", "cancel_rate",
    "bid_slope", "ask_slope", "volatility",
    "trade_intensity", "buy_ratio",
]
TARGET = "label_5"

# ── Constants ──────────────────────────────────────────────────────────────

SCALE = 10_000
ONE_US = 1_000_000


# ── Helpers ────────────────────────────────────────────────────────────────

def to_fixed(price_str: str) -> int:
    return int(round(float(price_str) * SCALE))

def to_qty(qty_str: str) -> int:
    return int(float(qty_str))


# ── Extended OrderBook with v2 features ────────────────────────────────────

class OrderBook:
    def __init__(self):
        self.bids: dict[int, int] = {}
        self.asks: dict[int, int] = {}
        self.seq = 0
        self._ofi = 0
        self._midprice_history: deque[float] = deque(maxlen=10)

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

    # ── v2 features ─────────────────────────────────────────────────────

    def bid_slope(self) -> float:
        """Linear regression: level_index ~ log(volume) across top 5 bid levels."""
        levels = sorted(self.bids.items(), key=lambda x: -x[0])[:5]
        if len(levels) < 3:
            return 0.0
        xs = np.arange(len(levels), dtype=float)
        ys = np.log(np.clip([q for _, q in levels], 1, None)).astype(float)
        cov = np.cov(xs, ys, ddof=0)[0, 1]
        var = np.var(xs)
        return cov / var if var > 1e-12 else 0.0

    def ask_slope(self) -> float:
        """Linear regression: level_index ~ log(volume) across top 5 ask levels."""
        levels = sorted(self.asks.items(), key=lambda x: x[0])[:5]
        if len(levels) < 3:
            return 0.0
        xs = np.arange(len(levels), dtype=float)
        ys = np.log(np.clip([q for _, q in levels], 1, None)).astype(float)
        cov = np.cov(xs, ys, ddof=0)[0, 1]
        var = np.var(xs)
        return cov / var if var > 1e-12 else 0.0

    def volatility(self) -> float:
        if len(self._midprice_history) < 3:
            return 0.0
        arr = np.array(self._midprice_history)
        returns = np.diff(np.log(np.maximum(arr, 1e-12)))
        if len(returns) < 2:
            return 0.0
        return float(np.std(returns, ddof=1))

    def update_midprice_history(self):
        mp = self.midprice()
        if not math.isnan(mp) and mp > 0:
            self._midprice_history.append(mp)


# ── Live Strategy ──────────────────────────────────────────────────────────

class LiveStrategy:
    def __init__(self, models: dict, scaler):
        self.models = models
        self.scaler = scaler
        self.book = OrderBook()

        self.event_count = 0
        self.snapshot_count = 0
        self.correct = 0
        self.wrong = 0
        self.pnl = 0.0
        self.position = 0  # 1 = long, -1 = short, 0 = flat
        self.last_signal_prob = 0.5
        self.last_prediction_time = 0
        self.last_features: dict = {}

        self.arrival_times: deque[int] = deque()
        self.cancel_times: deque[int] = deque()

    async def run(self, duration_sec: int = 300, threshold: float = 0.55):
        print(f"Connecting to Binance WebSocket (BTCUSDT depth)...", flush=True)
        async with websockets.connect(
            "wss://stream.binance.com:9443/ws/btcusdt@depth@100ms"
        ) as ws:
            # ── REST snapshot ─────────────────────────────────────────────
            resp = requests.get(
                "https://api.binance.com/api/v3/depth",
                params={"symbol": "BTCUSDT", "limit": 100},
            )
            snap = resp.json()
            last_update_id = snap["lastUpdateId"]
            self.book.apply_snapshot(snap["bids"], snap["asks"])
            self.book.update_midprice_history()

            # ── Buffer and sync ───────────────────────────────────────────
            print("Buffering initial events...", flush=True)
            buffer = []
            for _ in range(20):
                try:
                    msg = await asyncio.wait_for(ws.recv(), timeout=3.0)
                    ev = json.loads(msg)
                    if "U" in ev:
                        buffer.append(ev)
                except asyncio.TimeoutError:
                    break

            applied = 0
            for ev in buffer:
                if ev["U"] > last_update_id or (
                    ev["U"] <= last_update_id + 1 and ev["u"] > last_update_id
                ):
                    self.book.apply_diff(ev["b"], ev["a"])
                    applied += 1
            print(f"Applied {applied}/{len(buffer)} buffered events", flush=True)

            # ── Live loop ─────────────────────────────────────────────────
            start_time = time.monotonic()
            horizon_events = 5  # label_5
            pred_queue: deque = deque()  # (prob_buy, midprice_at_prediction)

            print(f"┌─ LIVE STRATEGY ── BTCUSDT ── Model: LR/{TARGET} ── Threshold: {threshold} ─┐", file=sys.stderr, flush=True)

            while time.monotonic() - start_time < duration_sec:
                try:
                    msg = await asyncio.wait_for(ws.recv(), timeout=0.5)
                except asyncio.TimeoutError:
                    self._dashboard()
                    continue

                ev = json.loads(msg)
                if "U" not in ev:
                    continue

                self.event_count += 1
                ts_us = int(time.time() * ONE_US)

                # Count arrivals/cancels
                n_arrivals, n_cancels = 0, 0
                for p, q in ev["b"] + ev["a"]:
                    if float(q) > 0:
                        n_arrivals += 1
                    else:
                        n_cancels += 1

                self.arrival_times.extend([ts_us] * n_arrivals)
                self.cancel_times.extend([ts_us] * n_cancels)
                cutoff = ts_us - ONE_US
                while self.arrival_times and self.arrival_times[0] < cutoff:
                    self.arrival_times.popleft()
                while self.cancel_times and self.cancel_times[0] < cutoff:
                    self.cancel_times.popleft()

                self.book.apply_diff(ev["b"], ev["a"])
                self.book.update_midprice_history()

                # Features + prediction at every event
                mp = self.book.midprice()
                if math.isnan(mp) or mp <= 0:
                    continue

                self.snapshot_count += 1

                features = np.array([[
                    mp,
                    self.book.spread() or 0,
                    self.book.microprice() or 0,
                    float(self.book.ofi()),
                    self.book.queue_imbalance(),
                    float(len(self.arrival_times)),
                    float(len(self.cancel_times)),
                    self.book.bid_slope(),
                    self.book.ask_slope(),
                    self.book.volatility(),
                    0.0,  # trade_intensity (Binance depth only)
                    0.0,  # buy_ratio (Binance depth only)
                ]], dtype=np.float64)
                features = np.nan_to_num(features, nan=0.0)

                prob = self.models["lr"].predict_proba(
                    self.scaler.transform(features)
                )[0, 1]

                self.last_signal_prob = prob
                self.last_prediction_time = ts_us
                self.last_features = dict(zip(FEATURE_NAMES, features[0]))
                self.position = 1 if prob > threshold else (-1 if prob < 1 - threshold else 0)
                self.book.reset_ofi()

                # PnL tracking: compare prediction vs actual movement after 5 events
                pred_queue.append((prob, mp))
                if len(pred_queue) > horizon_events:
                    past_prob, past_mid = pred_queue.popleft()
                    if mp > past_mid:
                        actual_up = True
                    else:
                        actual_up = False
                    predicted_up = past_prob > threshold

                    if predicted_up == actual_up:
                        self.correct += 1
                        if predicted_up:
                            self.pnl += 1.0
                        else:
                            self.pnl += 1.0
                    else:
                        self.wrong += 1
                        self.pnl -= 1.0

                # Dashboard every 10 events
                if self.event_count % 10 == 0:
                    self._dashboard()

            elapsed = time.monotonic() - start_time
            self._dashboard()
            print(f"\n--- Session Complete: {elapsed:.0f}s, {self.event_count} events ---", flush=True)

    def _dashboard(self):
        book = self.book
        bb, ba = book.best_bid / SCALE, book.best_ask / SCALE
        mp = book.midprice()
        spread = book.spread()
        total = self.correct + self.wrong
        acc = self.correct / total if total > 0 else 0.0

        signal = "BULLISH" if self.last_signal_prob > 0.55 else ("BEARISH" if self.last_signal_prob < 0.45 else "NEUTRAL")

        lines = [
            f"├─ BTCUSDT ─────────────────────────────────────────────────────",
            f"│ Best Bid: {bb:.2f} ({book.best_bid_qty})  Ask: {ba:.2f} ({book.best_ask_qty})  Mid: {mp:.2f}  Spread: {spread:.2f}",
            f"│ Signal: {signal:>8}  (prob_up={self.last_signal_prob:.3f})  Position: {'LONG' if self.position==1 else 'SHORT' if self.position==-1 else 'FLAT'}",
            f"│ PnL: {self.pnl:>+6.1f} bp  Accuracy: {acc:.1%}  ({self.correct}/{total})",
            f"│ Events: {self.event_count}  Snapshots: {self.snapshot_count}",
        ]
        for name in ["bid_slope", "ask_slope", "volatility", "ofi"]:
            val = self.last_features.get(name, 0)
            lines.append(f"│   {name:>12s} = {val:>10.4f}")
        lines.append(f"└──────────────────────────────────────────────────────────────")
        for l in lines:
            print(l, file=sys.stderr, flush=True)


# ── Main ───────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="Live BTCUSDT strategy demo")
    parser.add_argument("--duration", type=int, default=300, help="Run duration in seconds")
    parser.add_argument("--threshold", type=float, default=0.55, help="Prediction threshold (0-1)")
    parser.add_argument("--model-dir", type=str, default=str(MODEL_DIR))
    args = parser.parse_args()

    model_dir = Path(args.model_dir)
    print(f"Loading models from {model_dir}...", flush=True)

    scaler = joblib.load(model_dir / f"scaler_{TARGET}.pkl")
    models = {
        "lr": joblib.load(model_dir / f"logistic_regression_{TARGET}.pkl"),
    }

    strategy = LiveStrategy(models, scaler)
    try:
        asyncio.run(strategy.run(
            duration_sec=args.duration,
            threshold=args.threshold,
        ))
    except KeyboardInterrupt:
        print("\nInterrupted.", flush=True)


if __name__ == "__main__":
    main()
