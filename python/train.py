"""Train models for next-tick direction prediction."""

import sys
import json
from pathlib import Path

import polars as pl
import numpy as np
from sklearn.linear_model import LogisticRegression
from sklearn.preprocessing import StandardScaler
from sklearn.metrics import roc_auc_score, accuracy_score, classification_report
import xgboost as xgb
import lightgbm as lgb
import joblib


FEATURE_COLS = [
    "midprice", "spread", "microprice", "ofi",
    "queue_imbalance", "arrival_rate", "cancel_rate",
    "bid_slope", "ask_slope", "volatility",
    "trade_intensity", "buy_ratio",
]


def load_dataset(path: str) -> pl.DataFrame:
    p = Path(path)
    if p.suffix == ".parquet":
        return pl.read_parquet(p)
    return pl.read_csv(p)


def train_test_split_chronological(df: pl.DataFrame, train_frac: float = 0.8):
    n = len(df)
    split = int(n * train_frac)
    return df[:split], df[split:]


def train(
    data_path: str,
    target: str = "label_1",
    output_dir: str = "../models",
):
    out = Path(output_dir)
    out.mkdir(parents=True, exist_ok=True)

    # ── Load ────────────────────────────────────────────────────────────
    df = load_dataset(data_path)
    print(f"Loaded {len(df)} rows")

    # Ensure labels exist
    if target not in df.columns:
        # Run features.py inline
        from features import add_labels
        df = add_labels(df)
        print(f"Generated labels: {[c for c in df.columns if 'label_' in c]}")

    # Chronological split
    train_df, test_df = train_test_split_chronological(df)
    print(f"Train: {len(train_df)}, Test: {len(test_df)}")

    X_train = train_df.select(FEATURE_COLS).to_numpy().astype(np.float64)
    y_train = train_df[target].to_numpy().astype(np.int32)
    X_test  = test_df.select(FEATURE_COLS).to_numpy().astype(np.float64)
    y_test  = test_df[target].to_numpy().astype(np.int32)

    # Handle NaNs
    X_train = np.nan_to_num(X_train, nan=0.0)
    X_test  = np.nan_to_num(X_test,  nan=0.0)

    # Scale for logistic regression
    scaler = StandardScaler()
    X_train_s = scaler.fit_transform(X_train)
    X_test_s  = scaler.transform(X_test)

    results = {}

    # ── Logistic Regression ──────────────────────────────────────────────
    print("\n--- Logistic Regression ---")
    lr = LogisticRegression(C=1.0, class_weight="balanced", max_iter=1000)
    lr.fit(X_train_s, y_train)
    lr_pred = lr.predict_proba(X_test_s)[:, 1]
    lr_auc = roc_auc_score(y_test, lr_pred)
    results["logistic_regression"] = {"auc": round(lr_auc, 4)}
    print(f"  AUC: {lr_auc:.4f}")
    joblib.dump(lr, out / f"logistic_regression_{target}.pkl")
    joblib.dump(scaler, out / f"scaler_{target}.pkl")

    # ── XGBoost ──────────────────────────────────────────────────────────
    print("\n--- XGBoost ---")
    xgb_model = xgb.XGBClassifier(
        n_estimators=200,
        max_depth=6,
        learning_rate=0.1,
        subsample=0.8,
        colsample_bytree=0.8,
        eval_metric="logloss",
        use_label_encoder=False,
        verbosity=0,
    )
    xgb_model.fit(X_train, y_train, eval_set=[(X_test, y_test)], verbose=False)
    xgb_pred = xgb_model.predict_proba(X_test)[:, 1]
    xgb_auc = roc_auc_score(y_test, xgb_pred)
    results["xgboost"] = {"auc": round(xgb_auc, 4)}
    print(f"  AUC: {xgb_auc:.4f}")
    xgb_model.save_model(str(out / f"xgboost_{target}.json"))

    # Feature importance
    imp = xgb_model.feature_importances_
    for name, score in sorted(zip(FEATURE_COLS, imp), key=lambda x: -x[1]):
        print(f"    {name}: {score:.4f}")

    # ── LightGBM ─────────────────────────────────────────────────────────
    print("\n--- LightGBM ---")
    lgb_model = lgb.LGBMClassifier(
        n_estimators=200,
        max_depth=-1,
        num_leaves=31,
        learning_rate=0.1,
        subsample=0.8,
        colsample_bytree=0.8,
        verbosity=-1,
    )
    lgb_model.fit(X_train, y_train, eval_set=[(X_test, y_test)])
    lgb_pred = lgb_model.predict_proba(X_test)[:, 1]
    lgb_auc = roc_auc_score(y_test, lgb_pred)
    results["lightgbm"] = {"auc": round(lgb_auc, 4)}
    print(f"  AUC: {lgb_auc:.4f}")
    lgb_model.booster_.save_model(str(out / f"lightgbm_{target}.txt"))

    # ── Summary ──────────────────────────────────────────────────────────
    print("\n--- Summary ---")
    for name, r in results.items():
        print(f"  {name:25s}  AUC: {r['auc']:.4f}")

    with open(out / f"results_{target}.json", "w") as f:
        json.dump(results, f, indent=2)
    print(f"\nResults saved to {out}/")

    return results


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python train.py <dataset.parquet> [target_col] [output_dir]")
        sys.exit(1)
    target = sys.argv[2] if len(sys.argv) > 2 else "label_1"
    outdir = sys.argv[3] if len(sys.argv) > 3 else "../models"
    train(sys.argv[1], target=target, output_dir=outdir)
