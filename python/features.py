"""Label generation: forward returns and direction targets."""

import polars as pl
import numpy as np


def add_labels(
    df: pl.DataFrame,
    horizon_ticks: list[int] | None = None,
) -> pl.DataFrame:
    """Add forward-return labels to a market snapshot DataFrame.
    
    Expects columns: ts_us, midprice (and optionally spread for sanity checks).
    Adds:
      - next_return_N   : continuous forward return over N ticks
      - label_N         : bool, mid(t+N) > mid(t)
    """
    if horizon_ticks is None:
        horizon_ticks = [1, 5, 10]

    df = df.sort("ts_us")

    mid = df["midprice"].to_numpy()

    for h in horizon_ticks:
        # Shifted midprice h steps forward
        fwd = np.roll(mid, -h)
        fwd[-h:] = np.nan  # last h rows have no forward data

        ret = (fwd - mid) / np.maximum(mid, 1e-12)
        label = fwd > mid

        df = df.with_columns(
            pl.Series(f"next_return_{h}", ret),
            pl.Series(f"label_{h}", label),
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
