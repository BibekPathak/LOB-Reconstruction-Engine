"""Train a queue-position fill-probability model.

Simulates placing a hypothetical order at a price level and predicts
whether it would fill within 1ms / 5ms / 10ms based on book state features.
"""

import sys
from pathlib import Path

import polars as pl
import numpy as np
from sklearn.model_selection import train_test_split
from sklearn.metrics import roc_auc_score, brier_score_loss
import xgboost as xgb
import lightgbm as lgb
import joblib
import json


FEATURE_COLS = [
    "depth_ahead", "level_imbalance",
    "top_imbalance", "spread", "midprice",
    "arrival_rate", "cancel_rate", "ofi",
]

TARGET_HORIZONS = ["fill_1ms", "fill_5ms", "fill_10ms"]


def simulate_order_features(
    snapshots: pl.DataFrame,
    order_price_pct: float = 0.01,
    order_qty: int = 100,
    simulate_side: str = "buy",
) -> pl.DataFrame:
    """Generate queue-position training data from historical snapshots.
    
    For each snapshot, simulates a hypothetical order and extracts:
      - depth_ahead: total qty at the simulated order price
      - level_imbalance: bid/(bid+ask) volume ratio at that price
      - Market features copied from the snapshot
    
    Labels are determined by looking forward to see if the level's volume
    decreases (indicating fills) within the horizon.
    
    This is a simplified simulation — for production you'd replay the
    full order-book event stream.
    """
    rows = []

    for i in range(len(snapshots)):
        row = snapshots.row(i)

        # Current book state
        bid_px = row[snapshots.columns.index("best_bid")]
        ask_px = row[snapshots.columns.index("best_ask")]
        bid_qty = row[snapshots.columns.index("best_bid_qty")]
        ask_qty = row[snapshots.columns.index("best_ask_qty")]

        if bid_px == 0 or ask_px == 0:
            continue

        # Simulate a limit order at best-opposite + 1 tick
        tick = 100  # 0.01 in fixed-point (SCALE=10000)
        if simulate_side == "buy":
            order_px = ask_px + tick  # join the best ask
        else:
            order_px = bid_px - tick  # join the best bid

        # Feature: depth_ahead — qty at that price (same side)
        depth_ahead = bid_qty if simulate_side == "buy" else ask_qty

        # Feature: level imbalance at this price (both sides)
        opp_qty = ask_qty if simulate_side == "buy" else bid_qty
        total = depth_ahead + opp_qty
        level_imb = depth_ahead / total if total > 0 else 0.5

        # Market features from snapshot
        top_imb = row[snapshots.columns.index("queue_imbalance")]
        spread = row[snapshots.columns.index("spread")]
        midprice = row[snapshots.columns.index("midprice")]
        arrival = row[snapshots.columns.index("arrival_rate")]
        cancel = row[snapshots.columns.index("cancel_rate")]
        ofi = row[snapshots.columns.index("ofi")]

        # Labels: look forward to see if qty at order price decreases
        fill_1ms = False
        fill_5ms = False
        fill_10ms = False

        ts_now = row[snapshots.columns.index("ts_us")]
        for j in range(i + 1, min(i + 100, len(snapshots))):
            fwd_row = snapshots.row(j)
            ts_fwd = fwd_row[snapshots.columns.index("ts_us")]
            dt = ts_fwd - ts_now

            # Check if qty at this level decreased from our simulated order
            fwd_qty = fwd_row[snapshots.columns.index("best_bid_qty")] if simulate_side == "buy" \
                else fwd_row[snapshots.columns.index("best_ask_qty")]

            if fwd_qty < depth_ahead - order_qty:
                # Our simulated order (partially) filled
                if dt <= 1_000 and not fill_1ms:
                    fill_1ms = True
                if dt <= 5_000 and not fill_5ms:
                    fill_5ms = True
                if dt <= 10_000 and not fill_10ms:
                    fill_10ms = True
                break

        rows.append({
            "depth_ahead": depth_ahead,
            "level_imbalance": level_imb,
            "top_imbalance": top_imb,
            "spread": spread,
            "midprice": midprice,
            "arrival_rate": arrival,
            "cancel_rate": cancel,
            "ofi": ofi,
            "fill_1ms": fill_1ms,
            "fill_5ms": fill_5ms,
            "fill_10ms": fill_10ms,
            "ts_us": ts_now,
        })

    return pl.DataFrame(rows)


def train(
    data_path: str,
    output_dir: str = "../models",
):
    out = Path(output_dir)
    out.mkdir(parents=True, exist_ok=True)

    # Load snapshot data
    p = Path(data_path)
    df = pl.read_parquet(p) if p.suffix == ".parquet" else pl.read_csv(p)
    print(f"Loaded {len(df)} rows")

    # Check if we have fill labels already, or need to simulate
    has_labels = all(c in df.columns for c in TARGET_HORIZONS)

    if not has_labels:
        print("Simulating queue-position features...")
        df = simulate_order_features(df)
        print(f"Generated {len(df)} training samples")

    results = {}

    for target in TARGET_HORIZONS:
        print(f"\n--- Target: {target} ---")

        # Filter valid rows
        train_df = df.filter(pl.col(target).is_not_null())
        if len(train_df) < 10:
            print(f"  Too few samples ({len(train_df)}), skipping")
            continue

        X = train_df.select(FEATURE_COLS).to_numpy().astype(np.float64)
        y = train_df[target].to_numpy().astype(np.int32)
        X = np.nan_to_num(X, nan=0.0)

        # Split (chronological — last 20% as test)
        n = len(X)
        split = int(n * 0.8)
        X_train, X_test = X[:split], X[split:]
        y_train, y_test = y[:split], y[split:]

        if y_test.sum() == 0 or y_test.sum() == len(y_test):
            print(f"  Test set has only one class ({y_test.sum()}/{len(y_test)}), AUC undefined")

        # XGBoost
        model = xgb.XGBClassifier(
            n_estimators=200, max_depth=6, learning_rate=0.1,
            subsample=0.8, colsample_bytree=0.8,
            eval_metric="logloss", verbosity=0,
        )
        model.fit(X_train, y_train)

        y_pred = model.predict_proba(X_test)[:, 1]
        auc = roc_auc_score(y_test, y_pred) if len(np.unique(y_test)) > 1 else float("nan")
        brier = brier_score_loss(y_test, y_pred)

        results[target] = {"auc": round(auc, 4), "brier": round(brier, 4)}
        print(f"  AUC: {auc:.4f}  Brier: {brier:.4f}")

        model.save_model(str(out / f"queue_{target}.json"))

        # Feature importance
        imp = model.feature_importances_
        for name, score in sorted(zip(FEATURE_COLS, imp), key=lambda x: -x[1]):
            print(f"    {name}: {score:.4f}")

    print("\n--- Summary ---")
    for h, r in results.items():
        print(f"  {h:15s}  AUC: {r['auc']:.4f}  Brier: {r['brier']:.4f}")

    with open(out / "queue_results.json", "w") as f:
        json.dump(results, f, indent=2)
    print(f"\nResults saved to {out}/")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python train_queue_model.py <snapshots.parquet>")
        sys.exit(1)
    train(sys.argv[1])
