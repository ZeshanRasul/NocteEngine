# build_qtable_from_csvs_spyder.py
#
# Build a 5-action Q-table from multiple CSV logs by taking the latest row
# per state from each CSV, then averaging the Q-values across CSVs.
#
# Outputs:
# - qtable_5actions.hpp
# - qtable_5actions.csv
# - qtable_5actions.npy

from pathlib import Path
from typing import List, Optional

import numpy as np
import pandas as pd


# =============================================================================
# USER SETTINGS
# =============================================================================

CSV_FILES = [
    r"./run1.csv",
    r"./run2.csv",
    r"./run3.csv",
    r"./run4.csv",
    r"./run5.csv",
    r"./run6.csv",
    r"./run7.csv",
    r"./run8.csv",
    r"./run9.csv",
    r"./run10.csv",
    r"./run1_1.csv",
    r"./run2_1.csv",
    r"./run3_1.csv",
    r"./run4_1.csv",
    r"./run5_1.csv",
]

STATE_COL = "State"

# Set to None if you want "latest row" to mean last row in file order.
ITERATION_COL = "Iteration"

# Option 1:
# If your CSV explicitly contains Q-value columns, list them here:
Q_COLS: Optional[List[str]] = ["Q0", "Q1", "Q2", "Q3", "Q4"]

# Option 2:
# If your last 5 numeric columns are the Q-values, set Q_COLS = None.
# Q_COLS = None

NUM_ACTIONS = 5

# How to combine the 5 CSVs:
# "mean" or "median"
AGGREGATE_MODE = "mean"

# Optional fixed max state. If None, inferred from data.
MAX_STATE = None

CLAMP_NEGATIVE_ZERO = True

OUTPUT_PREFIX = r"./qtable_5actions"


# =============================================================================
# IMPLEMENTATION
# =============================================================================

def find_q_columns(
    df: pd.DataFrame,
    q_cols: Optional[List[str]],
    num_actions: int,
    state_col: str,
    iteration_col: Optional[str],
) -> List[str]:
    if q_cols is not None and len(q_cols) > 0:
        missing = [c for c in q_cols if c not in df.columns]
        if missing:
            raise ValueError(f"Missing specified Q columns: {missing}")
        if len(q_cols) != num_actions:
            raise ValueError(
                f"Expected {num_actions} Q columns, got {len(q_cols)}"
            )
        return q_cols

    exclude = {state_col}
    if iteration_col is not None:
        exclude.add(iteration_col)

    numeric_cols = [
        c for c in df.columns
        if c not in exclude and pd.api.types.is_numeric_dtype(df[c])
    ]

    if len(numeric_cols) < num_actions:
        raise ValueError(
            f"Could not auto-detect {num_actions} Q columns. "
            f"Numeric columns found: {numeric_cols}"
        )

    return numeric_cols[-num_actions:]


def latest_rows_per_state(
    df: pd.DataFrame,
    state_col: str,
    iteration_col: Optional[str],
) -> pd.DataFrame:
    if state_col not in df.columns:
        raise ValueError(f"State column '{state_col}' not found.")

    df = df.copy()

    if iteration_col is not None:
        if iteration_col not in df.columns:
            raise ValueError(f"Iteration column '{iteration_col}' not found.")
        df = df.sort_values([state_col, iteration_col], kind="stable")
    else:
        df["_row_index_temp"] = np.arange(len(df), dtype=np.int64)
        df = df.sort_values([state_col, "_row_index_temp"], kind="stable")

    latest = df.groupby(state_col, as_index=False).tail(1).copy()

    if "_row_index_temp" in latest.columns:
        latest = latest.drop(columns=["_row_index_temp"])

    return latest


def aggregate_qtables(
    per_file_tables: List[pd.DataFrame],
    state_col: str,
    q_cols: List[str],
    aggregate_mode: str,
) -> pd.DataFrame:
    all_states = sorted(
        set().union(*[set(t[state_col].tolist()) for t in per_file_tables])
    )

    rows = []
    for state in all_states:
        q_vectors = []
        for table in per_file_tables:
            match = table[table[state_col] == state]
            if not match.empty:
                q_vectors.append(match.iloc[0][q_cols].to_numpy(dtype=np.float64))

        if not q_vectors:
            continue

        q_stack = np.stack(q_vectors, axis=0)
        if aggregate_mode == "mean":
            q_final = q_stack.mean(axis=0)
        elif aggregate_mode == "median":
            q_final = np.median(q_stack, axis=0)
        else:
            raise ValueError("AGGREGATE_MODE must be 'mean' or 'median'.")

        row = {state_col: int(state)}
        for i, qv in enumerate(q_final):
            row[f"Q{i}"] = float(qv)
        rows.append(row)

    out = pd.DataFrame(rows).sort_values(state_col).reset_index(drop=True)
    return out


def dataframe_to_dense_array(
    df: pd.DataFrame,
    state_col: str,
    num_actions: int,
    max_state: Optional[int],
) -> np.ndarray:
    inferred_max = int(df[state_col].max()) if not df.empty else 0
    final_max = inferred_max if max_state is None else max(max_state, inferred_max)

    arr = np.zeros((final_max + 1, num_actions), dtype=np.float32)

    for _, row in df.iterrows():
        s = int(row[state_col])
        for a in range(num_actions):
            arr[s, a] = np.float32(row[f"Q{a}"])

    return arr


def format_float_cpp(v: float, clamp_negative_zero: bool) -> str:
    if clamp_negative_zero and abs(v) < 1e-12:
        v = 0.0
    return f"{v:.9f}f"


def write_hpp(
    path: Path,
    arr: np.ndarray,
    clamp_negative_zero: bool = False,
) -> None:
    max_state = arr.shape[0] - 1
    num_actions = arr.shape[1]

    lines = []
    lines.append("#pragma once")
    lines.append("#include <array>")
    lines.append("#include <unordered_map>")
    lines.append("")
    lines.append(
        f"static const std::unordered_map<int, std::array<float, {num_actions}>> Q_TABLE = {{"
    )

    for state in range(max_state + 1):
        qvals = arr[state]
        if np.allclose(qvals, 0.0):
            continue

        q_str = ", ".join(
            format_float_cpp(float(v), clamp_negative_zero) for v in qvals
        )
        lines.append(f"    {{{state}, {{{q_str}}}}},")

    lines.append("};")
    lines.append("")
    lines.append(f"static const int Q_TABLE_MAX_STATE = {max_state};")
    lines.append(f"static const int Q_TABLE_NUM_ACTIONS = {num_actions};")

    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    csv_paths = [Path(p) for p in CSV_FILES]
    for p in csv_paths:
        if not p.exists():
            raise FileNotFoundError(f"CSV not found: {p}")

    per_file_tables = []
    detected_q_cols = None

    for csv_path in csv_paths:
        df = pd.read_csv(csv_path)

        q_cols = find_q_columns(
            df=df,
            q_cols=Q_COLS,
            num_actions=NUM_ACTIONS,
            state_col=STATE_COL,
            iteration_col=ITERATION_COL,
        )

        if detected_q_cols is None:
            detected_q_cols = q_cols

        latest = latest_rows_per_state(
            df=df,
            state_col=STATE_COL,
            iteration_col=ITERATION_COL,
        )

        latest = latest[[STATE_COL] + q_cols].copy()
        per_file_tables.append(latest)

        print(f"[INFO] {csv_path.name}: using Q columns {q_cols}, "
              f"{len(latest)} unique latest states")

    combined = aggregate_qtables(
        per_file_tables=per_file_tables,
        state_col=STATE_COL,
        q_cols=detected_q_cols,
        aggregate_mode=AGGREGATE_MODE,
    )

    dense = dataframe_to_dense_array(
        df=combined,
        state_col=STATE_COL,
        num_actions=NUM_ACTIONS,
        max_state=MAX_STATE,
    )

    output_prefix = Path(OUTPUT_PREFIX)
    output_csv = output_prefix.with_suffix(".csv")
    output_npy = output_prefix.with_suffix(".npy")
    output_hpp = output_prefix.with_suffix(".hpp")

    combined.to_csv(output_csv, index=False)
    np.save(output_npy, dense)
    write_hpp(output_hpp, dense, clamp_negative_zero=CLAMP_NEGATIVE_ZERO)

    print()
    print(f"[DONE] Wrote CSV: {output_csv}")
    print(f"[DONE] Wrote NPY: {output_npy}")
    print(f"[DONE] Wrote HPP: {output_hpp}")
    print(f"[DONE] Dense Q-table shape: {dense.shape}")


if __name__ == "__main__":
    main()