"""Label generation: forward returns and direction targets."""

import polars as pl
import numpy as np


def add_labels(
    df: pl.DataFrame,
    horizon_ticks: list[int] | None = None,
) -> pl.DataFrame:
    """Add forward-return labels to a market snapshot DataFrame.
    
    Expects columns: ts_us, midprice, microprice.
    Adds:
      - next_return_N        : continuous forward return over N ticks
      - label_N              : bool, mid(t+N) > mid(t)
      - microprice_change_N  : microprice(t+N) - microprice(t)
    """
    if horizon_ticks is None:
        horizon_ticks = [1, 5, 10]

    df = df.sort("ts_us")

    mid = df["midprice"].to_numpy()
    micro = df["microprice"].to_numpy() if "microprice" in df.columns else mid

    for h in horizon_ticks:
        fwd_mid = np.roll(mid, -h)
        fwd_mid[-h:] = np.nan

        fwd_micro = np.roll(micro, -h)
        fwd_micro[-h:] = np.nan

        ret = (fwd_mid - mid) / np.maximum(mid, 1e-12)
        label = fwd_mid > mid
        mp_change = fwd_micro - micro

        df = df.with_columns(
            pl.Series(f"next_return_{h}", ret),
            pl.Series(f"label_{h}", label),
            pl.Series(f"microprice_change_{h}", mp_change),
        )

    return df


def main():
    import sys
    from pathlib import Path

    if len(sys.argv) < 2:
        print("Usage: python features.py <input.parquet> [output.parquet]")
        sys.exit(1)

    src = Path(sys.argv[1])
    dst = Path(sys.argv[2]) if len(sys.argv) > 2 else src.parent / f"{src.stem}_labeled{src.suffix}"

    df = pl.read_parquet(src) if src.suffix == ".parquet" else pl.read_csv(src)
    print(f"Loaded {len(df)} rows from {src}")

    df = add_labels(df)
    print(f"Columns: {df.columns}")

    df.write_parquet(dst, compression="zstd")
    print(f"Wrote {dst}  ({len(df)} rows, {len(df.columns)} cols)")


if __name__ == "__main__":
    main()
