"""Evaluate trained models: AUC, hit rate, Sharpe, plots."""

import sys
import json
from pathlib import Path

import polars as pl
import numpy as np
from sklearn.metrics import (
    roc_auc_score, accuracy_score, precision_score, recall_score,
    confusion_matrix, roc_curve,
)
import joblib
import xgboost as xgb
import lightgbm as lgb
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


FEATURE_COLS = [
    "midprice", "spread", "microprice", "ofi",
    "queue_imbalance", "arrival_rate", "cancel_rate",
    "bid_slope", "ask_slope", "volatility",
    "trade_intensity", "buy_ratio",
]
TARGET = "label_1"


def load_model(path: str, name: str):
    p = Path(path)
    if name == "xgboost":
        model = xgb.XGBClassifier()
        model.load_model(str(p))
        return model
    elif name == "lightgbm":
        model = lgb.Booster(model_file=str(p))
        return model
    else:
        return joblib.load(p)


def simulate_pnl(prob_pred: np.ndarray, y_true: np.ndarray, threshold: float = 0.5):
    """Simple strategy: long when prob > threshold, short when < 1-threshold."""
    position = np.where(prob_pred > threshold, 1, -1)
    # Reward: correct direction = +1, wrong = -1
    reward = np.where((position > 0) == (y_true > 0), 1, -1)
    sharpe = reward.mean() / max(reward.std(), 1e-12) * np.sqrt(252 * 6.5 * 60 * 60)
    return sharpe, reward


def evaluate(data_path: str, model_dir: str = "../models", output_dir: str = "../notebooks"):
    data_dir = Path(data_path).parent
    out = Path(output_dir)
    out.mkdir(parents=True, exist_ok=True)

    # Load data
    p = Path(data_path)
    df = pl.read_parquet(p) if p.suffix == ".parquet" else pl.read_csv(p)

    if TARGET not in df.columns:
        from features import add_labels
        df = add_labels(df)

    n = len(df)
    split = int(n * 0.8)
    test_df = df[split:]
    X_test = test_df.select(FEATURE_COLS).to_numpy().astype(np.float64)
    y_test = test_df[TARGET].to_numpy().astype(np.int32)
    X_test = np.nan_to_num(X_test, nan=0.0)

    models_dir = Path(model_dir)
    model_files = {
        "Logistic Regression": models_dir / "logistic_regression.pkl",
        "XGBoost": models_dir / "xgboost.json",
        "LightGBM": models_dir / "lightgbm.txt",
    }

    plt.figure(figsize=(12, 8))
    metrics = {}

    for name, mpath in model_files.items():
        if not mpath.exists():
            print(f"  Skipping {name}: {mpath} not found")
            continue

        print(f"\n--- {name} ---")
        model = load_model(str(mpath), name.lower().split()[0])

        if name == "LightGBM":
            prob_pred = model.predict(X_test)
            y_pred = (prob_pred > 0.5).astype(int)
        elif name == "XGBoost":
            prob_pred = model.predict_proba(X_test)[:, 1]
            y_pred = model.predict(X_test)
        else:
            scaler = models_dir / "scaler.pkl"
            if scaler.exists():
                X_test_s = joblib.load(scaler).transform(X_test)
            else:
                X_test_s = X_test
            prob_pred = model.predict_proba(X_test_s)[:, 1]
            y_pred = model.predict(X_test_s)

        auc = roc_auc_score(y_test, prob_pred)
        acc = accuracy_score(y_test, y_pred)
        prec = precision_score(y_test, y_pred, zero_division=0)
        rec = recall_score(y_test, y_pred, zero_division=0)
        sharpe, _ = simulate_pnl(prob_pred, y_test)
        cm = confusion_matrix(y_test, y_pred)

        metrics[name] = {
            "auc": round(auc, 4),
            "accuracy": round(acc, 4),
            "precision": round(prec, 4),
            "recall": round(rec, 4),
            "sharpe": round(sharpe, 4),
        }

        print(f"  AUC:       {auc:.4f}")
        print(f"  Accuracy:  {acc:.4f}")
        print(f"  Precision: {prec:.4f}")
        print(f"  Recall:    {rec:.4f}")
        print(f"  Sharpe:    {sharpe:.2f}")
        print(f"  Confusion:\n    {cm}")

        # ROC curve
        fpr, tpr, _ = roc_curve(y_test, prob_pred)
        plt.plot(fpr, tpr, label=f"{name} (AUC={auc:.3f})")

    plt.plot([0, 1], [0, 1], "k--", alpha=0.3)
    plt.xlabel("False Positive Rate")
    plt.ylabel("True Positive Rate")
    plt.title("ROC Curves")
    plt.legend()
    plt.grid(alpha=0.3)
    plt.savefig(out / "roc_curves.png", dpi=150)
    print(f"\nROC saved to {out / 'roc_curves.png'}")

    # Metrics table
    with open(out / "metrics.json", "w") as f:
        json.dump(metrics, f, indent=2)

    print("\n--- Metrics ---")
    for name, m in metrics.items():
        print(f"  {name:25s}  AUC={m['auc']:.4f}  Sharpe={m['sharpe']:.2f}  Acc={m['accuracy']:.4f}")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python evaluate.py <dataset.parquet> [model_dir] [output_dir]")
        sys.exit(1)
    evaluate(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else "../models",
             sys.argv[3] if len(sys.argv) > 3 else "../notebooks")
