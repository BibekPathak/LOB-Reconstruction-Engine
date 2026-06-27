"""Simple backtest: simulate trading strategy from model predictions."""

import sys
from pathlib import Path

import polars as pl
import numpy as np
import xgboost as xgb
import lightgbm as lgb
import joblib
import json


FEATURE_COLS = [
    "midprice", "spread", "microprice", "ofi",
    "queue_imbalance", "arrival_rate", "cancel_rate",
]


def backtest(
    data_path: str,
    model_path: str,
    model_name: str = "xgboost",
    threshold: float = 0.5,
    transaction_cost_bps: float = 0.0,
):
    """Backtest a trained model on historical data.

    Strategy:
      - If P(mid_up) > threshold: go long at next timestamp
      - If P(mid_up) < 1 - threshold: go short
      - PnL is market return scaled by position direction
    """
    p = Path(data_path)
    df = pl.read_parquet(p) if p.suffix == ".parquet" else pl.read_csv(p)

    from features import add_labels
    df = add_labels(df)
    df = df.sort("ts_us")

    n = len(df)
    split = int(n * 0.8)
    test_df = df[split:].clone()

    X = test_df.select(FEATURE_COLS).to_numpy().astype(np.float64)
    X = np.nan_to_num(X, nan=0.0)

    # Load model
    if model_name == "xgboost":
        model = xgb.XGBClassifier()
        model.load_model(model_path)
        prob = model.predict_proba(X)[:, 1]
    elif model_name == "lightgbm":
        model = lgb.Booster(model_file=model_path)
        prob = model.predict(X)
    else:
        scaler = Path(model_path).parent / "scaler.pkl"
        if scaler.exists():
            X = joblib.load(scaler).transform(X)
        model = joblib.load(model_path)
        prob = model.predict_proba(X)[:, 1]

    # Position: 1 (long) if prob > threshold, -1 (short) if prob < 1-threshold, 0 otherwise
    position = np.where(prob > threshold, 1, np.where(prob < 1 - threshold, -1, 0))

    # Returns: label_1 gives direction for next period
    actual_ret = test_df["next_return_1"].to_numpy()
    actual_ret = np.nan_to_num(actual_ret, nan=0.0)

    # Strategy PnL
    strategy_ret = position * actual_ret

    # Transaction costs
    turnover = np.abs(np.diff(position, prepend=0))
    tc = turnover * transaction_cost_bps * 1e-4
    strategy_ret -= tc

    cum_pnl = np.cumprod(1 + strategy_ret)
    sharpe = strategy_ret.mean() / max(strategy_ret.std(), 1e-12) * np.sqrt(252 * 6.5 * 60 * 60)
    hit_rate = np.mean((position > 0) == (actual_ret > 0))
    max_dd = np.maximum.accumulate(cum_pnl).max() - 1  # simplified

    # Summary
    results = {
        "sharpe": round(sharpe, 3),
        "hit_rate": round(hit_rate, 4),
        "total_return": round(cum_pnl[-1] - 1, 4),
        "max_drawdown": round(max_dd, 4),
        "threshold": threshold,
        "n_trades": int(np.sum(np.abs(position) > 0)),
        "long_pct": round(np.mean(position == 1), 4),
        "short_pct": round(np.mean(position == -1), 4),
    }

    print("--- Backtest Results ---")
    for k, v in results.items():
        print(f"  {k:20s}  {v}")

    return results, cum_pnl, strategy_ret


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("data", help="Path to dataset parquet/csv")
    parser.add_argument("--model", default="../models/xgboost.json")
    parser.add_argument("--name", default="xgboost", choices=["xgboost", "lightgbm", "logistic_regression"])
    parser.add_argument("--threshold", type=float, default=0.5)
    parser.add_argument("--tc", type=float, default=0.0, help="Transaction cost in bps")
    args = parser.parse_args()

    results, _, _ = backtest(args.data, args.model, args.name, args.threshold, args.tc)

    out = Path("../models/backtest_results.json")
    with open(out, "w") as f:
        json.dump(results, f, indent=2)
    print(f"\nSaved to {out}")
