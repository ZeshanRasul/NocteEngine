import os
import math
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

# ============================================================
# Config: change only these paths and settings
# ============================================================

RL_CSV_PATH = r"./Run_05/RL_Train.csv"
BASELINE_CSV_PATH = r"./Run_05/Baseline.csv"
# Example:

OUTPUT_DIR = r"./Run_05/train_analysis_outputs"

ROLLING_WINDOW = 2000
TOP_N_STATES = 10

# If True, only keep rows where Valid == 1
USE_VALID_ONLY = True

# If True, drop rows with NaN in key columns
DROP_NAN_ROWS = True


# ============================================================
# Helpers
# ============================================================

def ensure_dir(path):
    os.makedirs(path, exist_ok=True)


def save_fig(path):
    plt.tight_layout()
    plt.savefig(path, dpi=200, bbox_inches="tight")
    plt.close()


def rolling_mean(series, window):
    return series.rolling(window=window, min_periods=1).mean()


def load_metrics(csv_path):
    df = pd.read_csv(csv_path)

    if DROP_NAN_ROWS:
        key_cols = [c for c in [
            "Iteration", "State", "Action", "Raw Reward", "Reward",
            "Old Error", "New Error", "Q0", "Q1", "Q2"
        ] if c in df.columns]
        df = df.dropna(subset=key_cols)

    if USE_VALID_ONLY and "Valid" in df.columns:
        df = df[df["Valid"] == 1].copy()

    df = df.sort_values("Iteration").reset_index(drop=True)
    return df


def print_basic_summary(df, name="Run"):
    print("=" * 70)
    print(name)
    print("=" * 70)
    print(f"Rows: {len(df)}")
    print(f"Unique states: {df['State'].nunique() if 'State' in df.columns else 'N/A'}")

    if "Action" in df.columns:
        print("\nAction counts:")
        print(df["Action"].value_counts().sort_index())

    for col in ["Raw Reward", "Reward", "Old Error", "New Error", "Q0", "Q1", "Q2"]:
        if col in df.columns:
            s = df[col]
            print(f"\n{col}:")
            print(s.describe(percentiles=[0.5, 0.9, 0.95, 0.99]))


# ============================================================
# Plotting functions
# ============================================================

def plot_reward_histograms(df, output_dir, prefix="rl"):
    if "Raw Reward" in df.columns:
        plt.figure(figsize=(8, 5))
        plt.hist(df["Raw Reward"], bins=100)
        plt.xlabel("Raw Reward")
        plt.ylabel("Count")
        plt.title("Raw Reward Histogram")
        save_fig(os.path.join(output_dir, f"{prefix}_raw_reward_hist.png"))

    if "Reward" in df.columns:
        plt.figure(figsize=(8, 5))
        plt.hist(df["Reward"], bins=100)
        plt.xlabel("Reward")
        plt.ylabel("Count")
        plt.title("Final Reward Histogram")
        save_fig(os.path.join(output_dir, f"{prefix}_reward_hist.png"))


def plot_reward_over_iteration(df, output_dir, prefix="rl"):
    if "Reward" not in df.columns:
        return

    plt.figure(figsize=(10, 5))
    plt.plot(df["Iteration"], df["Reward"], alpha=0.3, label="Reward")
    plt.plot(df["Iteration"], rolling_mean(df["Reward"], ROLLING_WINDOW), label=f"Rolling Mean ({ROLLING_WINDOW})")
    plt.xlabel("Iteration")
    plt.ylabel("Reward")
    plt.title("Reward Over Iteration")
    plt.legend()
    save_fig(os.path.join(output_dir, f"{prefix}_reward_over_iteration.png"))

    if "Raw Reward" in df.columns:
        plt.figure(figsize=(10, 5))
        plt.plot(df["Iteration"], df["Raw Reward"], alpha=0.3, label="Raw Reward")
        plt.plot(df["Iteration"], rolling_mean(df["Raw Reward"], ROLLING_WINDOW), label=f"Rolling Mean ({ROLLING_WINDOW})")
        plt.xlabel("Iteration")
        plt.ylabel("Raw Reward")
        plt.title("Raw Reward Over Iteration")
        plt.legend()
        save_fig(os.path.join(output_dir, f"{prefix}_raw_reward_over_iteration.png"))


def plot_mean_reward_by_action(df, output_dir, prefix="rl"):
    if "Action" not in df.columns or "Reward" not in df.columns:
        return

    grouped = df.groupby("Action")["Reward"].mean().sort_index()

    plt.figure(figsize=(7, 5))
    plt.bar(grouped.index.astype(str), grouped.values)
    plt.xlabel("Action")
    plt.ylabel("Mean Reward")
    plt.title("Mean Reward by Action")
    save_fig(os.path.join(output_dir, f"{prefix}_mean_reward_by_action.png"))


def plot_mean_raw_reward_by_action(df, output_dir, prefix="rl"):
    if "Action" not in df.columns or "Raw Reward" not in df.columns:
        return

    grouped = df.groupby("Action")["Raw Reward"].mean().sort_index()

    plt.figure(figsize=(7, 5))
    plt.bar(grouped.index.astype(str), grouped.values)
    plt.xlabel("Action")
    plt.ylabel("Mean Raw Reward")
    plt.title("Mean Raw Reward by Action")
    save_fig(os.path.join(output_dir, f"{prefix}_mean_raw_reward_by_action.png"))


def plot_top_state_action_reward_heatmap(df, output_dir, prefix="rl", top_n=10):
    if "State" not in df.columns or "Action" not in df.columns or "Reward" not in df.columns:
        return

    top_states = df["State"].value_counts().head(top_n).index.tolist()

    heat = (
        df[df["State"].isin(top_states)]
        .groupby(["State", "Action"])["Reward"]
        .mean()
        .unstack(fill_value=0.0)
        .sort_index()
    )

    plt.figure(figsize=(8, max(4, 0.5 * len(heat))))
    plt.imshow(heat.values, aspect="auto")
    plt.colorbar(label="Mean Reward")
    plt.xticks(range(len(heat.columns)), [str(c) for c in heat.columns])
    plt.yticks(range(len(heat.index)), [str(s) for s in heat.index])
    plt.xlabel("Action")
    plt.ylabel("State")
    plt.title(f"Mean Reward by State-Action (Top {top_n} States)")
    save_fig(os.path.join(output_dir, f"{prefix}_state_action_reward_heatmap.png"))


def plot_top_state_action_frequency_heatmap(df, output_dir, prefix="rl", top_n=10):
    if "State" not in df.columns or "Action" not in df.columns:
        return

    top_states = df["State"].value_counts().head(top_n).index.tolist()

    heat = (
        df[df["State"].isin(top_states)]
        .groupby(["State", "Action"])
        .size()
        .unstack(fill_value=0)
        .sort_index()
    )

    plt.figure(figsize=(8, max(4, 0.5 * len(heat))))
    plt.imshow(heat.values, aspect="auto")
    plt.colorbar(label="Count")
    plt.xticks(range(len(heat.columns)), [str(c) for c in heat.columns])
    plt.yticks(range(len(heat.index)), [str(s) for s in heat.index])
    plt.xlabel("Action")
    plt.ylabel("State")
    plt.title(f"Action Frequency by State (Top {top_n} States)")
    save_fig(os.path.join(output_dir, f"{prefix}_state_action_frequency_heatmap.png"))


def plot_q_value_evolution(df, output_dir, prefix="rl"):
    q_cols = [c for c in ["Q0", "Q1", "Q2"] if c in df.columns]
    if not q_cols:
        return

    plt.figure(figsize=(10, 5))
    for q in q_cols:
        plt.plot(df["Iteration"], rolling_mean(df[q], ROLLING_WINDOW), label=q)
    plt.xlabel("Iteration")
    plt.ylabel("Q Value")
    plt.title(f"Q Value Evolution (Rolling Mean {ROLLING_WINDOW})")
    plt.legend()
    save_fig(os.path.join(output_dir, f"{prefix}_q_value_evolution.png"))


def plot_final_best_action_per_state(df, output_dir, prefix="rl", top_n=20):
    q_cols = [c for c in ["Q0", "Q1", "Q2"] if c in df.columns]
    if len(q_cols) < 3 or "State" not in df.columns:
        return

    final_rows = df.groupby("State", as_index=False).tail(1).copy()
    final_rows["BestAction"] = final_rows[q_cols].values.argmax(axis=1)

    top_states = df["State"].value_counts().head(top_n).index.tolist()
    final_rows = final_rows[final_rows["State"].isin(top_states)].copy()
    final_rows = final_rows.sort_values("State")

    plt.figure(figsize=(10, 5))
    plt.bar(final_rows["State"].astype(str), final_rows["BestAction"])
    plt.xlabel("State")
    plt.ylabel("Best Action")
    plt.title(f"Best Final Action per State (Top {top_n} Visited States)")
    save_fig(os.path.join(output_dir, f"{prefix}_best_action_per_state.png"))


def plot_error_comparison(df, output_dir, prefix="rl"):
    if "Old Error" not in df.columns or "New Error" not in df.columns:
        return

    plt.figure(figsize=(10, 5))
    plt.plot(df["Iteration"], rolling_mean(df["Old Error"], ROLLING_WINDOW), label="Old Error")
    plt.plot(df["Iteration"], rolling_mean(df["New Error"], ROLLING_WINDOW), label="New Error")
    plt.xlabel("Iteration")
    plt.ylabel("Error")
    plt.title(f"Old vs New Error (Rolling Mean {ROLLING_WINDOW})")
    plt.legend()
    save_fig(os.path.join(output_dir, f"{prefix}_old_vs_new_error.png"))

    improvement = df["Old Error"] - df["New Error"]
    plt.figure(figsize=(10, 5))
    plt.plot(df["Iteration"], rolling_mean(improvement, ROLLING_WINDOW), label="Old Error - New Error")
    plt.xlabel("Iteration")
    plt.ylabel("Improvement")
    plt.title(f"Error Improvement (Rolling Mean {ROLLING_WINDOW})")
    plt.legend()
    save_fig(os.path.join(output_dir, f"{prefix}_error_improvement.png"))


def plot_reward_boxplot_by_action(df, output_dir, prefix="rl"):
    if "Action" not in df.columns or "Reward" not in df.columns:
        return

    actions = sorted(df["Action"].unique())
    data = [df.loc[df["Action"] == a, "Reward"].values for a in actions]

    plt.figure(figsize=(8, 5))
    plt.boxplot(data, labels=[str(a) for a in actions], showfliers=False)
    plt.xlabel("Action")
    plt.ylabel("Reward")
    plt.title("Reward Distribution by Action")
    save_fig(os.path.join(output_dir, f"{prefix}_reward_boxplot_by_action.png"))


def plot_rl_vs_baseline_reward(df_rl, df_base, output_dir):
    if "Reward" not in df_rl.columns or "Reward" not in df_base.columns:
        return

    plt.figure(figsize=(10, 5))
    plt.plot(df_rl["Iteration"], rolling_mean(df_rl["Reward"], ROLLING_WINDOW), label="RL Reward")
    plt.plot(df_base["Iteration"], rolling_mean(df_base["Reward"], ROLLING_WINDOW), label="Baseline Reward")
    plt.xlabel("Iteration")
    plt.ylabel("Reward")
    plt.title("RL vs Baseline Reward")
    plt.legend()
    save_fig(os.path.join(output_dir, "rl_vs_baseline_reward.png"))


def plot_rl_vs_baseline_error(df_rl, df_base, output_dir):
    if "New Error" not in df_rl.columns or "New Error" not in df_base.columns:
        return

    plt.figure(figsize=(10, 5))
    plt.plot(df_rl["Iteration"], rolling_mean(df_rl["New Error"], ROLLING_WINDOW), label="RL New Error")
    plt.plot(df_base["Iteration"], rolling_mean(df_base["New Error"], ROLLING_WINDOW), label="Baseline New Error")
    plt.xlabel("Iteration")
    plt.ylabel("New Error")
    plt.title("RL vs Baseline New Error")
    plt.legend()
    save_fig(os.path.join(output_dir, "rl_vs_baseline_new_error.png"))


# ============================================================
# Main
# ============================================================

def main():
    ensure_dir(OUTPUT_DIR)

    df_rl = load_metrics(RL_CSV_PATH)
    print_basic_summary(df_rl, "RL Run")

    plot_reward_histograms(df_rl, OUTPUT_DIR, prefix="rl")
    plot_reward_over_iteration(df_rl, OUTPUT_DIR, prefix="rl")
    plot_mean_reward_by_action(df_rl, OUTPUT_DIR, prefix="rl")
    plot_mean_raw_reward_by_action(df_rl, OUTPUT_DIR, prefix="rl")
    plot_top_state_action_reward_heatmap(df_rl, OUTPUT_DIR, prefix="rl", top_n=TOP_N_STATES)
    plot_top_state_action_frequency_heatmap(df_rl, OUTPUT_DIR, prefix="rl", top_n=TOP_N_STATES)
    plot_q_value_evolution(df_rl, OUTPUT_DIR, prefix="rl")
    plot_final_best_action_per_state(df_rl, OUTPUT_DIR, prefix="rl", top_n=TOP_N_STATES)
    plot_error_comparison(df_rl, OUTPUT_DIR, prefix="rl")
    plot_reward_boxplot_by_action(df_rl, OUTPUT_DIR, prefix="rl")

    if BASELINE_CSV_PATH is not None and os.path.exists(BASELINE_CSV_PATH):
        df_base = load_metrics(BASELINE_CSV_PATH)
        print_basic_summary(df_base, "Baseline Run")

        plot_rl_vs_baseline_reward(df_rl, df_base, OUTPUT_DIR)
        plot_rl_vs_baseline_error(df_rl, df_base, OUTPUT_DIR)

    print("\nSaved plots to:", OUTPUT_DIR)


if __name__ == "__main__":
    main()