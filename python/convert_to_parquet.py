"""Convert CSV dataset to Parquet using PyArrow."""

import sys
from pathlib import Path

import pyarrow as pa
import pyarrow.parquet as pq
import pyarrow.csv as pcsv


def convert(csv_path: str, parquet_path: str | None = None) -> str:
    csv_path = Path(csv_path)
    if parquet_path is None:
        parquet_path = csv_path.with_suffix(".parquet")
    else:
        parquet_path = Path(parquet_path)

    table = pcsv.read_csv(csv_path)
    pq.write_table(table, parquet_path, compression="zstd")
    print(f"Wrote {parquet_path}  ({table.num_rows} rows, {table.num_columns} cols)")
    return str(parquet_path)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python convert_to_parquet.py <input.csv> [output.parquet]")
        sys.exit(1)
    convert(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
