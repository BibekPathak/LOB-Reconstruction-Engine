"""Generate all report figures: ROC, SHAP, confusion, PnL, queue calibration."""

import sys
import json
from pathlib import Path

import polars as pl
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import seaborn as sns
from sklearn.metrics import roc_curve, auc, confusion_matrix
from sklearn.preprocessing import StandardScaler
import xgboost as xgb
import lightgbm as lgb
import joblib

sns.set_theme(style="whitegrid")

FEATURE_COLS = [
    "midprice", "spread", "microprice", "ofi",
    "queue_imbalance", "arrival_rate", "cancel_rate",
    "bid_slope", "ask_slope", "volatility",
    "trade_intensity", "buy_ratio",
]

TARGETS = ["label_1", "label_5", "label_10"]
TARGET_LABELS = {"label_1": "1-tick", "label_5": "5-tick", "label_10": "10-tick"}


def load_test_data(data_path: str, target_col: str) -> tuple:
    df = pl.read_parquet(data_path) if Path(data_path).suffix == ".parquet" else pl.read_csv(data_path)
    if target_col not in df.columns:
        from features import add_labels
        df = add_labels(df)
    train_df, test_df = df[:int(len(df)*0.8)], df[int(len(df)*0.8):]
    X_test = test_df.select(FEATURE_COLS).to_numpy().astype(np.float64)
    y_test = test_df[target_col].to_numpy().astype(np.int32)
    X_test = np.nan_to_num(X_test, nan=0.0)
    return X_test, y_test, test_df["ts_us"].to_numpy() if "ts_us" in test_df.columns else None


def plot_roc_curves(models, model_names, X_test_dict, y_test_dict, output_path: str):
    fig, ax = plt.subplots(figsize=(8, 6))
    colors = {"logistic_regression": "blue", "xgboost": "green", "lightgbm": "orange"}

    for target in TARGETS:
        for name in model_names:
            if name not in models.get(target, {}):
                continue
            model = models[target][name]
            X_test = X_test_dict[target]
            y_test = y_test_dict[target]
            if len(np.unique(y_test)) < 2:
                continue
            y_pred = model.predict_proba(X_test)[:, 1]
            fpr, tpr, _ = roc_curve(y_test, y_pred)
            roc_auc = auc(fpr, tpr)
            label = f"{TARGET_LABELS[target]} {name.split('_')[0]} (AUC={roc_auc:.3f})"
            ax.plot(fpr, tpr, label=label, color=colors.get(name, "gray"), lw=1.5, alpha=0.8)

    ax.plot([0, 1], [0, 1], "k--", lw=1, alpha=0.5)
    ax.set_xlim(0, 1)
    ax.set_ylim(0, 1)
    ax.set_xlabel("False Positive Rate")
    ax.set_ylabel("True Positive Rate")
    ax.set_title("ROC Curves")
    ax.legend(fontsize=8, loc="lower right")
    fig.tight_layout()
    fig.savefig(output_path, dpi=150)
    plt.close(fig)
    print(f"  Saved {output_path}")


def plot_feature_importance(model, model_name, feature_names, output_path: str):
    if hasattr(model, "feature_importances_"):
        imp = model.feature_importances_
    elif hasattr(model, "coef_"):
        imp = np.abs(model.coef_[0])
    else:
        return

    fig, ax = plt.subplots(figsize=(8, 5))
    idx = np.argsort(imp)
    ax.barh([feature_names[i] for i in idx], imp[idx])
    ax.set_xlabel("Importance")
    ax.set_title(f"Feature Importance — {model_name}")
    fig.tight_layout()
    fig.savefig(output_path, dpi=150)
    plt.close(fig)
    print(f"  Saved {output_path}")


def plot_shap_summary(model, model_name, X_test, feature_names, output_path: str):
    try:
        import shap
    except ImportError:
        print("  shap not installed, skipping SHAP")
        return

    if X_test.shape[0] < 5 or X_test.shape[1] < 2:
        print(f"  Skipping SHAP for {model_name}: too few samples/dims")
        return

    try:
        explainer = shap.TreeExplainer(model)
        shap_values = explainer.shap_values(X_test)

        if isinstance(shap_values, list):
            shap_values = shap_values[1]

        fig = plt.figure(figsize=(10, 6))
        shap.summary_plot(shap_values, X_test, feature_names=feature_names, show=False)
        plt.tight_layout()
        fig.savefig(output_path, dpi=150, bbox_inches="tight")
        plt.close(fig)
        print(f"  Saved {output_path}")
    except Exception as e:
        print(f"  SHAP failed for {model_name}: {e}")


def plot_shap_dependence(models, model_names, X_test_dict, feature_names, output_path: str):
    try:
        import shap
    except ImportError:
        return

    top_features = feature_names[:3]
    fig, axes = plt.subplots(1, 3, figsize=(15, 4))

    for idx, feat in enumerate(top_features):
        ax = axes[idx]
        for name in model_names:
            if name not in models.get(TARGETS[0], {}):
                continue
            model = models[TARGETS[0]][name]
            X_test = X_test_dict[TARGETS[0]]
            if X_test.shape[0] < 5:
                continue
            try:
                explainer = shap.TreeExplainer(model)
                shap_values = explainer.shap_values(X_test)
                if isinstance(shap_values, list):
                    shap_values = shap_values[1]
                fi = feature_names.index(feat)
                ax.scatter(X_test[:, fi], shap_values[:, fi], s=8, alpha=0.4, label=name)
            except Exception:
                continue
        ax.set_xlabel(feat)
        ax.set_ylabel("SHAP value")
        ax.set_title(feat)
        ax.legend(fontsize=7)

    fig.suptitle("SHAP Dependence — Top 3 Features", fontsize=13)
    fig.tight_layout()
    fig.savefig(output_path, dpi=150)
    plt.close(fig)
    print(f"  Saved {output_path}")


def plot_confusion_matrices(models, model_names, X_test_dict, y_test_dict, output_path: str):
    n_targets = len(TARGETS)
    fig, axes = plt.subplots(n_targets, max(len(model_names), 1), figsize=(12, 10))
    if n_targets == 1:
        axes = [axes]

    for i, target in enumerate(TARGETS):
        for j, name in enumerate(model_names):
            ax = axes[i][j] if n_targets > 1 else axes[j]
            if name not in models.get(target, {}):
                ax.text(0.5, 0.5, "N/A", ha="center", va="center")
                continue
            model = models[target][name]
            X_test = X_test_dict[target]
            y_test = y_test_dict[target]
            y_pred = (model.predict_proba(X_test)[:, 1] > 0.5).astype(int)
            cm = confusion_matrix(y_test, y_pred)
            cm_norm = cm.astype("float") / cm.sum(axis=1)[:, np.newaxis]
            sns.heatmap(cm_norm, annot=True, fmt=".2f", cmap="Blues", ax=ax,
                        xticklabels=["Down", "Up"], yticklabels=["Down", "Up"])
            ax.set_title(f"{TARGET_LABELS[target]} — {name.split('_')[0]}")
            ax.set_xlabel("Predicted")
            ax.set_ylabel("True")

    fig.suptitle("Normalized Confusion Matrices", fontsize=14)
    fig.tight_layout()
    fig.savefig(output_path, dpi=150)
    plt.close(fig)
    print(f"  Saved {output_path}")


def plot_pnl_curve(models, model_names, X_test_dict, y_test_dict, ts_test_dict, output_path: str):
    fig, ax = plt.subplots(figsize=(10, 5))

    for target in TARGETS:
        for name in model_names:
            if name not in models.get(target, {}):
                continue
            model = models[target][name]
            X_test = X_test_dict[target]
            y_test = y_test_dict[target]
            y_pred = model.predict_proba(X_test)[:, 1]
            # Long when prob > 0.5, short otherwise
            position = np.where(y_pred > 0.5, 1, -1)
            ret = position * np.where(y_test == 1, 1, -1)
            pnl = np.cumsum(ret) * 0.0001  # 1bp per win
            label = f"{TARGET_LABELS[target]} {name.split('_')[0]}"
            ax.plot(pnl, label=label, lw=1, alpha=0.8)

    ax.set_xlabel("Trade Number")
    ax.set_ylabel("Cumulative PnL (bp)")
    ax.set_title("Backtest PnL — Direction Strategy")
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(output_path, dpi=150)
    plt.close(fig)
    print(f"  Saved {output_path}")


def plot_queue_calibration(data_path: str, output_path: str):
    try:
        from train_queue_model import simulate_order_features
        df = pl.read_parquet(data_path) if Path(data_path).suffix == ".parquet" else pl.read_csv(data_path)
        df = simulate_order_features(df)

        results_data = []
        for horizon in ["fill_1ms", "fill_5ms", "fill_10ms"]:
            if horizon not in df.columns:
                continue
            train_df, test_df = df[:int(len(df)*0.8)], df[int(len(df)*0.8):]

            from train_queue_model import FEATURE_COLS as Q_FEATURE_COLS
            X_train = train_df.select(Q_FEATURE_COLS).to_numpy().astype(np.float64)
            y_train = train_df[horizon].to_numpy().astype(np.int32)
            X_test = test_df.select(Q_FEATURE_COLS).to_numpy().astype(np.float64)
            y_test = test_df[horizon].to_numpy().astype(np.int32)
            X_train = np.nan_to_num(X_train, nan=0.0)
            X_test = np.nan_to_num(X_test, nan=0.0)

            if len(np.unique(y_train)) < 2:
                continue

            model = xgb.XGBClassifier(n_estimators=100, max_depth=4, eval_metric="logloss", verbosity=0)
            model.fit(X_train, y_train)
            y_pred = model.predict_proba(X_test)[:, 1]

            # Calibration curve
            n_bins = 10
            bins = np.linspace(0, 1, n_bins + 1)
            bin_centers = (bins[:-1] + bins[1:]) / 2
            bin_indices = np.digitize(y_pred, bins) - 1
            bin_indices = np.clip(bin_indices, 0, n_bins - 1)

            empirical = np.array([y_test[bin_indices == i].mean() for i in range(n_bins) if (bin_indices == i).sum() > 0])
            predicted = np.array([y_pred[bin_indices == i].mean() for i in range(n_bins) if (bin_indices == i).sum() > 0])
            counts = np.array([(bin_indices == i).sum() for i in range(n_bins)])

            results_data.append({
                "horizon": horizon,
                "predicted": predicted,
                "empirical": empirical,
                "counts": counts,
                "bins": bin_centers[:len(predicted)],
            })

        if not results_data:
            print("  No calibration data generated")
            return

        fig, ax = plt.subplots(figsize=(8, 6))
        colors = {"fill_1ms": "blue", "fill_5ms": "green", "fill_10ms": "orange"}
        for r in results_data:
            ax.plot(r["predicted"], r["empirical"], "o-", label=r["horizon"], color=colors.get(r["horizon"], "gray"))
        ax.plot([0, 1], [0, 1], "k--", alpha=0.5)
        ax.set_xlabel("Predicted Fill Probability")
        ax.set_ylabel("Empirical Fill Rate")
        ax.set_title("Queue Position Model — Calibration")
        ax.legend()
        fig.tight_layout()
        fig.savefig(output_path, dpi=150)
        plt.close(fig)
        print(f"  Saved {output_path}")
    except Exception as e:
        print(f"  Queue calibration failed: {e}")


def generate_all(data_path: str, model_dir: str, figure_dir: str):
    model_dir = Path(model_dir)
    figure_dir = Path(figure_dir)
    figure_dir.mkdir(parents=True, exist_ok=True)

    # ── Load models ──────────────────────────────────────────────────────
    model_names = ["logistic_regression", "xgboost", "lightgbm"]
    models = {}
    X_test_dict = {}
    y_test_dict = {}
    ts_test_dict = {}

    for target in TARGETS:
        models[target] = {}
        X_test_dict[target], y_test_dict[target], ts_test = load_test_data(data_path, target)
        ts_test_dict[target] = ts_test

        for name in model_names:
            key = f"{name}_{target}"
            try:
                if name == "logistic_regression":
                    scaler = joblib.load(model_dir / f"scaler_{target}.pkl")
                    lr = joblib.load(model_dir / f"logistic_regression_{target}.pkl")
                    X_test_s = scaler.transform(X_test_dict[target])
                    models[target][name] = (lambda m, s: type("Wrapper", (), {
                        "predict_proba": lambda self, X: m.predict_proba(s.transform(X))
                    })())(lr, scaler)
                    # Store the actual model for feature importance
                    models[target][name].feature_importances_ = np.abs(lr.coef_[0])
                elif name == "xgboost":
                    m = xgb.XGBClassifier()
                    m.load_model(str(model_dir / f"xgboost_{target}.json"))
                    models[target][name] = m
                elif name == "lightgbm":
                    booster = lgb.Booster(model_file=str(model_dir / f"lightgbm_{target}.txt"))
                    n_features = X_test_dict[target].shape[1]
                    class LightGBMWrapper:
                        def __init__(self, bst):
                            self._bst = bst
                            self.feature_importances_ = bst.feature_importance(importance_type="gain")
                        def predict_proba(self, X):
                            pred = self._bst.predict(X, num_iteration=self._bst.best_iteration)
                            return np.column_stack([1 - pred, pred])
                    m = LightGBMWrapper(booster)
                    models[target][name] = m
            except (FileNotFoundError, ValueError) as e:
                print(f"  Skipping {key}: {e}")
                continue

    # ── 1. ROC Curves ────────────────────────────────────────────────────
    print("\n[1/7] ROC Curves...")
    plot_roc_curves(models, model_names, X_test_dict, y_test_dict,
                    str(figure_dir / "roc_curves.png"))

    # ── 2. Feature Importance ────────────────────────────────────────────
    print("\n[2/7] Feature Importance...")
    for target in TARGETS:
        for name in model_names:
            if name not in models.get(target, {}):
                continue
            plot_feature_importance(
                models[target][name], f"{name}_{target}", FEATURE_COLS,
                str(figure_dir / f"feature_importance_{name}_{target}.png"),
            )

    # ── 3. SHAP Summary ──────────────────────────────────────────────────
    print("\n[3/7] SHAP Summary...")
    for target in TARGETS:
        for name in ["xgboost"]:
            if name not in models.get(target, {}):
                continue
            plot_shap_summary(
                models[target][name], f"{name}_{target}",
                X_test_dict[target], FEATURE_COLS,
                str(figure_dir / f"shap_summary_{name}_{target}.png"),
            )

    # ── 4. SHAP Dependence ───────────────────────────────────────────────
    print("\n[4/7] SHAP Dependence...")
    plot_shap_dependence(
        models, ["xgboost"], X_test_dict, FEATURE_COLS,
        str(figure_dir / "shap_dependence_top3.png"),
    )

    # ── 5. Confusion Matrices ────────────────────────────────────────────
    print("\n[5/7] Confusion Matrices...")
    plot_confusion_matrices(
        models, model_names, X_test_dict, y_test_dict,
        str(figure_dir / "confusion_matrices.png"),
    )

    # ── 6. PnL Curve ─────────────────────────────────────────────────────
    print("\n[6/7] PnL Curve...")
    plot_pnl_curve(
        models, model_names, X_test_dict, y_test_dict, ts_test_dict,
        str(figure_dir / "pnl_curve.png"),
    )

    # ── 7. Queue Calibration ─────────────────────────────────────────────
    print("\n[7/7] Queue Calibration...")
    plot_queue_calibration(
        data_path,
        str(figure_dir / "queue_calibration.png"),
    )

    print(f"\nAll figures saved to {figure_dir}/")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python analysis.py <dataset.parquet> [model_dir] [figure_dir]")
        sys.exit(1)
    generate_all(
        sys.argv[1],
        sys.argv[2] if len(sys.argv) > 2 else "../models",
        sys.argv[3] if len(sys.argv) > 3 else "../research/figures",
    )
