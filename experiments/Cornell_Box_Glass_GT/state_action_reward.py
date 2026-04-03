import os
import numpy as np
import pandas as pd

# ============================================================
# Config
# ============================================================

RL_CSV_PATH = r"./Run_03/RL_Metrics.csv"
OUTPUT_DIR = r"./analysis_outputs"
TOP_N_STATES = 15
USE_VALID_ONLY = True

os.makedirs(OUTPUT_DIR, exist_ok=True)

# ============================================================
# Load
# ============================================================

df = pd.read_csv(RL_CSV_PATH)

if USE_VALID_ONLY and "Valid" in df.columns:
    df = df[df["Valid"] == 1].copy()

df = df.sort_values("Iteration").reset_index(drop=True)

required_cols = ["State", "Action", "Reward"]
for c in required_cols:
    if c not in df.columns:
        raise ValueError(f"Missing required column: {c}")

# ============================================================
# Per-state per-action summary
# ============================================================

summary = (
    df.groupby(["State", "Action"])
      .agg(
          count=("Reward", "size"),
          mean_reward=("Reward", "mean"),
          median_reward=("Reward", "median"),
          std_reward=("Reward", "std"),
          min_reward=("Reward", "min"),
          max_reward=("Reward", "max"),
      )
      .reset_index()
)

summary["std_reward"] = summary["std_reward"].fillna(0.0)

summary_path = os.path.join(OUTPUT_DIR, "per_state_action_summary.csv")
summary.to_csv(summary_path, index=False)

# ============================================================
# Pivot tables for easier comparison
# ============================================================

mean_pivot = summary.pivot(index="State", columns="Action", values="mean_reward")
median_pivot = summary.pivot(index="State", columns="Action", values="median_reward")
count_pivot = summary.pivot(index="State", columns="Action", values="count")

mean_pivot = mean_pivot.sort_index()
median_pivot = median_pivot.sort_index()
count_pivot = count_pivot.sort_index()

mean_pivot_path = os.path.join(OUTPUT_DIR, "per_state_action_mean_reward.csv")
median_pivot_path = os.path.join(OUTPUT_DIR, "per_state_action_median_reward.csv")
count_pivot_path = os.path.join(OUTPUT_DIR, "per_state_action_count.csv")

mean_pivot.to_csv(mean_pivot_path)
median_pivot.to_csv(median_pivot_path)
count_pivot.to_csv(count_pivot_path)

# ============================================================
# Best action + gap analysis
# ============================================================

rows = []
all_actions = sorted(df["Action"].unique())

for state in sorted(df["State"].unique()):
    state_rows = summary[summary["State"] == state].copy()

    mean_map = {int(r["Action"]): float(r["mean_reward"]) for _, r in state_rows.iterrows()}
    median_map = {int(r["Action"]): float(r["median_reward"]) for _, r in state_rows.iterrows()}
    count_map = {int(r["Action"]): int(r["count"]) for _, r in state_rows.iterrows()}

    mean_values = [(a, mean_map.get(a, np.nan)) for a in all_actions]
    median_values = [(a, median_map.get(a, np.nan)) for a in all_actions]

    valid_mean = [(a, v) for a, v in mean_values if not np.isnan(v)]
    valid_median = [(a, v) for a, v in median_values if not np.isnan(v)]

    valid_mean_sorted = sorted(valid_mean, key=lambda x: x[1], reverse=True)
    valid_median_sorted = sorted(valid_median, key=lambda x: x[1], reverse=True)

    best_mean_action = valid_mean_sorted[0][0] if valid_mean_sorted else None
    best_mean_reward = valid_mean_sorted[0][1] if valid_mean_sorted else np.nan
    second_mean_reward = valid_mean_sorted[1][1] if len(valid_mean_sorted) > 1 else np.nan
    mean_gap = best_mean_reward - second_mean_reward if len(valid_mean_sorted) > 1 else np.nan

    best_median_action = valid_median_sorted[0][0] if valid_median_sorted else None
    best_median_reward = valid_median_sorted[0][1] if valid_median_sorted else np.nan
    second_median_reward = valid_median_sorted[1][1] if len(valid_median_sorted) > 1 else np.nan
    median_gap = best_median_reward - second_median_reward if len(valid_median_sorted) > 1 else np.nan

    total_count = int(sum(count_map.values()))

    row = {
        "State": state,
        "TotalCount": total_count,
        "BestActionByMean": best_mean_action,
        "BestMeanReward": best_mean_reward,
        "SecondBestMeanReward": second_mean_reward,
        "MeanGap": mean_gap,
        "BestActionByMedian": best_median_action,
        "BestMedianReward": best_median_reward,
        "SecondBestMedianReward": second_median_reward,
        "MedianGap": median_gap,
    }

    for a in all_actions:
        row[f"Count_A{a}"] = count_map.get(a, 0)
        row[f"Mean_A{a}"] = mean_map.get(a, np.nan)
        row[f"Median_A{a}"] = median_map.get(a, np.nan)

    rows.append(row)

best_action_df = pd.DataFrame(rows).sort_values("TotalCount", ascending=False)

best_action_path = os.path.join(OUTPUT_DIR, "per_state_best_action_summary.csv")
best_action_df.to_csv(best_action_path, index=False)

# ============================================================
# Print top visited states
# ============================================================

top_states = best_action_df.head(TOP_N_STATES)

pd.set_option("display.width", 200)
pd.set_option("display.max_columns", 50)
pd.set_option("display.float_format", "{:.6f}".format)

print("\nTop visited states: per-state action summary\n")
print(
    top_states[
        ["State", "TotalCount",
         "BestActionByMean", "BestMeanReward", "SecondBestMeanReward", "MeanGap",
         "BestActionByMedian", "BestMedianReward", "SecondBestMedianReward", "MedianGap"]
        +
        [f"Count_A{a}" for a in all_actions]
        +
        [f"Mean_A{a}" for a in all_actions]
        +
        [f"Median_A{a}" for a in all_actions]
    ]
)

print("\nSaved files:")
print(summary_path)
print(mean_pivot_path)
print(median_pivot_path)
print(count_pivot_path)
print(best_action_path)