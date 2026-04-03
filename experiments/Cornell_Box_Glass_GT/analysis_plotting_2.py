import os
import math
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt


# ============================================================
# CONFIG
# ============================================================

# Input CSVs
RL_CSV = r"./Run_05/RL_Train.csv"
BASELINE_CSV = r"./Run_05/Baseline.csv"  

# Output folder
OUTPUT_DIR = r"./Run_02/analysis_outputs_2"

# Rolling window for convergence plots
ROLLING_WINDOW = 100

# Number of most visited states to analyse
TOP_N_STATES = 15

# Optional label names for actions
ACTION_LABELS = {
    0: "BSDF-heavy",
    1: "Balanced",
    2: "Light-heavy"
}

# ------------------------------------------------------------
# Column names in your CSV
# Change these if your export uses different names.
# ------------------------------------------------------------
COL_STATE = "State"
COL_ACTION = "Action"
COL_REWARD = "Reward"
COL_RAW_REWARD = "Raw Reward"
COL_OLD_ERROR = "Old Error"
COL_NEW_ERROR = "New Error"

# Optional iteration column. If missing, script creates one.
COL_ITERATION = "Iteration"

# Optional validity column. If missing, all rows are treated as valid.
COL_VALID = "Valid"

# Optional terminated column. Not required for this script.
COL_TERMINATED = "Terminated"


# ============================================================
# HELPERS
# ============================================================

def ensure_output_dir(path):
    os.makedirs(path, exist_ok=True)


def load_csv(path):
    df = pd.read_csv(path)
    return df


def add_iteration_if_missing(df):
    if COL_ITERATION not in df.columns:
        df = df.copy()
        df[COL_ITERATION] = np.arange(len(df))
    return df


def filter_valid_rows(df):
    if COL_VALID in df.columns:
        return df[df[COL_VALID] == 1].copy()
    return df.copy()


def rolling_mean(series, window):
    return series.rolling(window=window, min_periods=1).mean()


def save_table(df, filename):
    out_path = os.path.join(OUTPUT_DIR, filename)
    df.to_csv(out_path, index=False)
    print(f"Saved table: {out_path}")


def get_action_name(action_idx):
    return ACTION_LABELS.get(action_idx, str(action_idx))


def action_sort_key(action_value):
    try:
        return int(action_value)
    except Exception:
        return action_value


def compute_global_action_table(df):
    grouped = (
        df.groupby(COL_ACTION)[COL_REWARD]
        .agg(["count", "mean", "median", "std", "min", "max"])
        .reset_index()
        .rename(columns={
            COL_ACTION: "Action",
            "count": "Count",
            "mean": "MeanReward",
            "median": "MedianReward",
            "std": "StdReward",
            "min": "MinReward",
            "max": "MaxReward"
        })
    )
    grouped["ActionName"] = grouped["Action"].map(get_action_name)
    grouped = grouped[[
        "Action", "ActionName", "Count",
        "MeanReward", "MedianReward", "StdReward",
        "MinReward", "MaxReward"
    ]]
    grouped = grouped.sort_values("Action")
    return grouped


def compute_top_states(df, top_n=15):
    counts = (
        df.groupby(COL_STATE)
        .size()
        .reset_index(name="TotalCount")
        .sort_values("TotalCount", ascending=False)
    )
    top_states = counts.head(top_n)[COL_STATE].tolist()
    return top_states


def compute_state_action_summary(df, top_states):
    top_df = df[df[COL_STATE].isin(top_states)].copy()

    grouped = (
        top_df.groupby([COL_STATE, COL_ACTION])[COL_REWARD]
        .agg(["count", "mean", "median"])
        .reset_index()
        .rename(columns={
            "count": "Count",
            "mean": "MeanReward",
            "median": "MedianReward"
        })
    )

    mean_pivot = grouped.pivot(index=COL_STATE, columns=COL_ACTION, values="MeanReward")
    median_pivot = grouped.pivot(index=COL_STATE, columns=COL_ACTION, values="MedianReward")
    count_pivot = grouped.pivot(index=COL_STATE, columns=COL_ACTION, values="Count")

    mean_pivot = mean_pivot.sort_index()
    median_pivot = median_pivot.sort_index()
    count_pivot = count_pivot.sort_index()

    rows = []
    for state in mean_pivot.index:
        mean_row = mean_pivot.loc[state].dropna()
        median_row = median_pivot.loc[state].dropna() if state in median_pivot.index else pd.Series(dtype=float)
        count_row = count_pivot.loc[state].fillna(0) if state in count_pivot.index else pd.Series(dtype=float)

        if len(mean_row) == 0:
            continue

        mean_sorted = mean_row.sort_values(ascending=False)
        median_sorted = median_row.sort_values(ascending=False) if len(median_row) > 0 else pd.Series(dtype=float)

        best_action_mean = int(mean_sorted.index[0])
        best_mean = float(mean_sorted.iloc[0])
        second_best_mean = float(mean_sorted.iloc[1]) if len(mean_sorted) > 1 else np.nan
        mean_gap = best_mean - second_best_mean if len(mean_sorted) > 1 else np.nan

        if len(median_sorted) > 0:
            best_action_median = int(median_sorted.index[0])
            best_median = float(median_sorted.iloc[0])
            second_best_median = float(median_sorted.iloc[1]) if len(median_sorted) > 1 else np.nan
            median_gap = best_median - second_best_median if len(median_sorted) > 1 else np.nan
        else:
            best_action_median = np.nan
            best_median = np.nan
            second_best_median = np.nan
            median_gap = np.nan

        row = {
            "State": state,
            "TotalCount": int(count_row.sum()),
            "BestActionByMean": best_action_mean,
            "BestMeanReward": best_mean,
            "SecondBestMeanReward": second_best_mean,
            "MeanGap": mean_gap,
            "BestActionByMedian": best_action_median,
            "BestMedianReward": best_median,
            "SecondBestMedianReward": second_best_median,
            "MedianGap": median_gap
        }

        for action in sorted(ACTION_LABELS.keys()):
            row[f"Count_A{action}"] = int(count_row.get(action, 0))
            row[f"Mean_A{action}"] = float(mean_pivot.loc[state].get(action, np.nan))
            row[f"Median_A{action}"] = float(median_pivot.loc[state].get(action, np.nan))

        rows.append(row)

    summary_df = pd.DataFrame(rows)
    summary_df = summary_df.sort_values("TotalCount", ascending=False)
    return summary_df, mean_pivot, median_pivot, count_pivot


def plot_convergence_rl_vs_baseline(rl_df, baseline_df, use_col, ylabel, title, filename):
    plt.figure(figsize=(8, 5))

    rl_series = rolling_mean(rl_df[use_col], ROLLING_WINDOW)
    plt.plot(rl_df[COL_ITERATION], rl_series, label=f"RL {ylabel}")

    if baseline_df is not None:
        min_len = min(len(rl_df), len(baseline_df))
        plt.plot(
            baseline_df[COL_ITERATION].iloc[:min_len],
            rolling_mean(baseline_df[use_col].iloc[:min_len], ROLLING_WINDOW),
            label=f"Baseline {ylabel}"
        )

    plt.xlabel("Iteration")
    plt.ylabel(ylabel)
    plt.title(title)
    plt.legend()
    plt.tight_layout()

    out_path = os.path.join(OUTPUT_DIR, filename)
    plt.savefig(out_path, dpi=200)
    plt.close()
    print(f"Saved figure: {out_path}")


def plot_single_series(df, use_col, ylabel, title, filename):
    plt.figure(figsize=(8, 5))
    plt.plot(df[COL_ITERATION], df[use_col], alpha=0.2, label=ylabel)
    plt.plot(df[COL_ITERATION], rolling_mean(df[use_col], ROLLING_WINDOW), linewidth=2, label=f"Rolling Mean ({ROLLING_WINDOW})")
    plt.xlabel("Iteration")
    plt.ylabel(ylabel)
    plt.title(title)
    plt.legend()
    plt.tight_layout()

    out_path = os.path.join(OUTPUT_DIR, filename)
    plt.savefig(out_path, dpi=200)
    plt.close()
    print(f"Saved figure: {out_path}")


def plot_q_value_evolution(df, top_state, filename):
    top_state_df = df[df[COL_STATE] == top_state].copy()
    if top_state_df.empty:
        print(f"Warning: no rows found for state {top_state}, skipping Q evolution plot.")
        return

    plt.figure(figsize=(8, 5))

    for action in sorted(ACTION_LABELS.keys()):
        action_df = top_state_df[top_state_df[COL_ACTION] == action].copy()
        if action_df.empty:
            continue
        action_df = action_df.sort_values(COL_ITERATION)
        q_like = rolling_mean(action_df[COL_REWARD], ROLLING_WINDOW)
        plt.plot(action_df[COL_ITERATION], q_like, label=f"Q{action}")

    plt.xlabel("Iteration")
    plt.ylabel("Reward Rolling Mean")
    plt.title(f"Reward Evolution for Most Visited State {top_state}")
    plt.legend()
    plt.tight_layout()

    out_path = os.path.join(OUTPUT_DIR, filename)
    plt.savefig(out_path, dpi=200)
    plt.close()
    print(f"Saved figure: {out_path}")


def plot_heatmap(matrix_df, title, colorbar_label, filename):
    if matrix_df.empty:
        print(f"Warning: empty matrix for {filename}, skipping.")
        return

    data = matrix_df.copy()
    data = data.fillna(np.nan)

    plt.figure(figsize=(8, 6))
    im = plt.imshow(data.values, aspect="auto")
    plt.colorbar(im, label=colorbar_label)
    plt.xticks(range(len(data.columns)), [str(c) for c in data.columns])
    plt.yticks(range(len(data.index)), [str(i) for i in data.index])
    plt.xlabel("Action")
    plt.ylabel("State")
    plt.title(title)
    plt.tight_layout()

    out_path = os.path.join(OUTPUT_DIR, filename)
    plt.savefig(out_path, dpi=200)
    plt.close()
    print(f"Saved figure: {out_path}")


def plot_reward_distribution_by_action(df, use_col, title, filename):
    data = []
    labels = []

    for action in sorted(ACTION_LABELS.keys()):
        vals = df[df[COL_ACTION] == action][use_col].dropna().values
        if len(vals) == 0:
            continue
        data.append(vals)
        labels.append(str(action))

    if len(data) == 0:
        print(f"Warning: no data for {filename}, skipping.")
        return

    plt.figure(figsize=(8, 5))
    plt.boxplot(data, labels=labels)
    plt.xlabel("Action")
    plt.ylabel(use_col)
    plt.title(title)
    plt.tight_layout()

    out_path = os.path.join(OUTPUT_DIR, filename)
    plt.savefig(out_path, dpi=200)
    plt.close()
    print(f"Saved figure: {out_path}")


def plot_mean_reward_by_action(df, use_col, title, filename):
    grouped = (
        df.groupby(COL_ACTION)[use_col]
        .mean()
        .reset_index()
        .sort_values(COL_ACTION)
    )

    plt.figure(figsize=(7, 4.5))
    plt.bar(grouped[COL_ACTION].astype(str), grouped[use_col])
    plt.xlabel("Action")
    plt.ylabel(f"Mean {use_col}")
    plt.title(title)
    plt.tight_layout()

    out_path = os.path.join(OUTPUT_DIR, filename)
    plt.savefig(out_path, dpi=200)
    plt.close()
    print(f"Saved figure: {out_path}")


def plot_histogram(df, use_col, title, filename, bins=50):
    plt.figure(figsize=(8, 5))
    plt.hist(df[use_col].dropna(), bins=bins)
    plt.xlabel(use_col)
    plt.ylabel("Count")
    plt.title(title)
    plt.tight_layout()

    out_path = os.path.join(OUTPUT_DIR, filename)
    plt.savefig(out_path, dpi=200)
    plt.close()
    print(f"Saved figure: {out_path}")


def plot_per_state_gain(summary_df, filename):
    if summary_df.empty:
        print(f"Warning: empty summary dataframe for {filename}, skipping.")
        return

    gain_df = summary_df.sort_values("MeanGap", ascending=False).copy()

    plt.figure(figsize=(9, 5))
    plt.bar(gain_df["State"].astype(str), gain_df["MeanGap"])
    plt.xlabel("State")
    plt.ylabel("BestMeanReward - SecondBestMeanReward")
    plt.title("Per-State Reward Gap")
    plt.xticks(rotation=45)
    plt.tight_layout()

    out_path = os.path.join(OUTPUT_DIR, filename)
    plt.savefig(out_path, dpi=200)
    plt.close()
    print(f"Saved figure: {out_path}")


def plot_best_action_per_state(summary_df, filename):
    if summary_df.empty:
        print(f"Warning: empty summary dataframe for {filename}, skipping.")
        return

    plot_df = summary_df.sort_values("State").copy()

    plt.figure(figsize=(9, 5))
    plt.bar(plot_df["State"].astype(str), plot_df["BestActionByMean"])
    plt.xlabel("State")
    plt.ylabel("Best Action")
    plt.title("Best Final Action per State (Top Visited States)")
    plt.xticks(rotation=45)
    plt.tight_layout()

    out_path = os.path.join(OUTPUT_DIR, filename)
    plt.savefig(out_path, dpi=200)
    plt.close()
    print(f"Saved figure: {out_path}")


def print_top_states_summary(summary_df):
    pd.set_option("display.max_columns", None)
    pd.set_option("display.width", 200)
    print("\nTop visited states: per-state action summary\n")
    print(summary_df)


# ============================================================
# MAIN
# ============================================================

def main():
    ensure_output_dir(OUTPUT_DIR)

    # -----------------------------
    # Load data
    # -----------------------------
    rl_df = load_csv(RL_CSV)
    rl_df = add_iteration_if_missing(rl_df)
    rl_df = filter_valid_rows(rl_df)

    baseline_df = None
    if BASELINE_CSV is not None and os.path.exists(BASELINE_CSV):
        baseline_df = load_csv(BASELINE_CSV)
        baseline_df = add_iteration_if_missing(baseline_df)
        baseline_df = filter_valid_rows(baseline_df)

    # -----------------------------
    # Tables
    # -----------------------------
    global_action_table = compute_global_action_table(rl_df)
    save_table(global_action_table, "global_action_performance.csv")

    top_states = compute_top_states(rl_df, TOP_N_STATES)
    summary_df, mean_pivot, median_pivot, count_pivot = compute_state_action_summary(rl_df, top_states)

    save_table(summary_df, "per_state_action_summary.csv")
    save_table(
        mean_pivot.reset_index().rename(columns={COL_STATE: "State"}),
        "per_state_action_mean_reward.csv"
    )
    save_table(
        median_pivot.reset_index().rename(columns={COL_STATE: "State"}),
        "per_state_action_median_reward.csv"
    )
    save_table(
        count_pivot.reset_index().rename(columns={COL_STATE: "State"}),
        "per_state_action_count.csv"
    )

    best_action_table = summary_df[[
        "State", "TotalCount", "BestActionByMean", "BestMeanReward",
        "SecondBestMeanReward", "MeanGap",
        "BestActionByMedian", "BestMedianReward",
        "SecondBestMedianReward", "MedianGap"
    ]].copy()
    save_table(best_action_table, "per_state_best_action_summary.csv")

    print_top_states_summary(summary_df)

    # -----------------------------
    # Figures: convergence
    # -----------------------------
    if baseline_df is not None:
        plot_convergence_rl_vs_baseline(
            rl_df, baseline_df, COL_REWARD,
            ylabel="Reward",
            title="RL vs Baseline Reward",
            filename="fig_rl_vs_baseline_reward.png"
        )

        plot_convergence_rl_vs_baseline(
            rl_df, baseline_df, COL_NEW_ERROR,
            ylabel="New Error",
            title="RL vs Baseline New Error",
            filename="fig_rl_vs_baseline_new_error.png"
        )

    plot_single_series(
        rl_df, COL_RAW_REWARD,
        ylabel="Raw Reward",
        title="Raw Reward Over Iteration",
        filename="fig_raw_reward_over_iteration.png"
    )

    plot_single_series(
        rl_df, COL_REWARD,
        ylabel="Reward",
        title="Reward Over Iteration",
        filename="fig_reward_over_iteration.png"
    )

    if COL_OLD_ERROR in rl_df.columns and COL_NEW_ERROR in rl_df.columns:
        plot_convergence_rl_vs_baseline(
            rl_df, None, COL_OLD_ERROR,
            ylabel="Old Error",
            title="Old Error Rolling Mean",
            filename="fig_old_error.png"
        )
        plot_convergence_rl_vs_baseline(
            rl_df, None, COL_NEW_ERROR,
            ylabel="New Error",
            title="New Error Rolling Mean",
            filename="fig_new_error.png"
        )

        improvement_df = rl_df.copy()
        improvement_df["ErrorImprovement"] = improvement_df[COL_OLD_ERROR] - improvement_df[COL_NEW_ERROR]
        plot_single_series(
            improvement_df,
            use_col="ErrorImprovement",
            ylabel="Improvement",
            title="Error Improvement (Old Error - New Error)",
            filename="fig_error_improvement.png"
        )

        plt.figure(figsize=(8, 5))
        plt.plot(rl_df[COL_ITERATION], rolling_mean(rl_df[COL_OLD_ERROR], ROLLING_WINDOW), label="Old Error")
        plt.plot(rl_df[COL_ITERATION], rolling_mean(rl_df[COL_NEW_ERROR], ROLLING_WINDOW), label="New Error")
        plt.xlabel("Iteration")
        plt.ylabel("Error")
        plt.title("Old vs New Error (Rolling Mean)")
        plt.legend()
        plt.tight_layout()
        out_path = os.path.join(OUTPUT_DIR, "fig_old_vs_new_error.png")
        plt.savefig(out_path, dpi=200)
        plt.close()
        print(f"Saved figure: {out_path}")

    # -----------------------------
    # Figures: reward distributions
    # -----------------------------
    plot_mean_reward_by_action(
        rl_df, COL_RAW_REWARD,
        title="Mean Raw Reward by Action",
        filename="fig_mean_raw_reward_by_action.png"
    )

    plot_mean_reward_by_action(
        rl_df, COL_REWARD,
        title="Mean Reward by Action",
        filename="fig_mean_reward_by_action.png"
    )

    plot_reward_distribution_by_action(
        rl_df, COL_REWARD,
        title="Reward Distribution by Action",
        filename="fig_reward_distribution_by_action.png"
    )

    plot_histogram(
        rl_df, COL_RAW_REWARD,
        title="Raw Reward Histogram",
        filename="fig_raw_reward_histogram.png"
    )

    plot_histogram(
        rl_df, COL_REWARD,
        title="Final Reward Histogram",
        filename="fig_final_reward_histogram.png"
    )

    # -----------------------------
    # Figures: state-action analysis
    # -----------------------------
    plot_heatmap(
        count_pivot,
        title=f"Action Frequency by State (Top {TOP_N_STATES} States)",
        colorbar_label="Count",
        filename="fig_action_frequency_by_state.png"
    )

    plot_heatmap(
        mean_pivot,
        title=f"Mean Reward by State-Action (Top {TOP_N_STATES} States)",
        colorbar_label="Mean Reward",
        filename="fig_mean_reward_by_state_action.png"
    )

    plot_per_state_gain(
        summary_df,
        filename="fig_per_state_reward_gap.png"
    )

    plot_best_action_per_state(
        summary_df,
        filename="fig_best_action_per_state.png"
    )

    # -----------------------------
    # Figure: evolution for most visited state
    # -----------------------------
    if len(top_states) > 0:
        most_visited_state = top_states[0]
        plot_q_value_evolution(
            rl_df,
            top_state=most_visited_state,
            filename="fig_q_value_evolution_top_state.png"
        )

    print("\nSaved files:")
    for name in sorted(os.listdir(OUTPUT_DIR)):
        print(os.path.join(OUTPUT_DIR, name))


if __name__ == "__main__":
    main()