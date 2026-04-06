import os
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

# ============================================================
# CONFIG
# ============================================================

CSV_PATH = r"./ActionPlots/action_metrics.csv"
OUTPUT_DIR = r"./ActionPlots/action_freq_plots_clean"

# Use None to plot all scenes found
SCENES_TO_PLOT = None
# Example:
# SCENES_TO_PLOT = ["Diffuse_Sphere", "Diffuse_Alcove"]

# Optional action labels
ACTION_LABELS = ["Action 0", "Action 1", "Action 2", "Action 3", "Action 4"]

# Optional filter
FILTER_USING_RL = False
USING_RL_COLUMN = "UsingRL"
USING_RL_VALUE = 1

# Rolling smoothing window on already binned frequencies
SMOOTHING_WINDOW = 2   # 1 = no smoothing, 2 or 3 usually good

# Top states to show in state-action heatmap
TOP_N_STATES = 20

# ============================================================
# BINNING SETUP
# ============================================================

# Choose one:
USE_POWER_OF_TWO_BINS = True

# If False, use these explicit bins:
CUSTOM_BINS = [2, 4, 8, 16, 32, 64, 128, 256]

# ============================================================
# HELPERS
# ============================================================

def safe_name(name: str) -> str:
    return "".join(c if c.isalnum() or c in ("_", "-") else "_" for c in name)


def load_data(csv_path: str) -> pd.DataFrame:
    df = pd.read_csv(csv_path)

    required = {"FrameIndex", "Scene", "State", "Action"}
    missing = required - set(df.columns)
    if missing:
        raise ValueError(f"Missing required columns: {missing}")

    df["FrameIndex"] = pd.to_numeric(df["FrameIndex"], errors="raise").astype(int)
    df["Scene"] = df["Scene"].astype(str)
    df["State"] = pd.to_numeric(df["State"], errors="coerce").astype("Int64")
    df["Action"] = pd.to_numeric(df["Action"], errors="raise").astype(int)

    return df


def assign_power_of_two_bins(frame_indices: pd.Series) -> pd.Series:
    # Bin each frame index to the nearest lower power of two
    # e.g. 2->2, 3->2, 4->4, 5->4, 6->4, 7->4, 8->8
    vals = frame_indices.to_numpy()
    vals = np.maximum(vals, 1)
    bins = 2 ** np.floor(np.log2(vals))
    return pd.Series(bins.astype(int), index=frame_indices.index)


def assign_custom_bins(frame_indices: pd.Series, custom_bins: list[int]) -> pd.Series:
    # Map each frame index to nearest lower-or-equal custom bin
    sorted_bins = sorted(custom_bins)
    out = []

    for x in frame_indices:
        chosen = sorted_bins[0]
        for b in sorted_bins:
            if x >= b:
                chosen = b
            else:
                break
        out.append(chosen)

    return pd.Series(out, index=frame_indices.index, dtype=int)


def add_spp_bin(df: pd.DataFrame) -> pd.DataFrame:
    df = df.copy()

    if USE_POWER_OF_TWO_BINS:
        df["SPPBin"] = assign_power_of_two_bins(df["FrameIndex"])
    else:
        df["SPPBin"] = assign_custom_bins(df["FrameIndex"], CUSTOM_BINS)

    return df


def compute_action_frequencies(df: pd.DataFrame) -> pd.DataFrame:
    counts = (
        df.groupby(["Scene", "SPPBin", "Action"])
        .size()
        .reset_index(name="Count")
    )

    totals = (
        counts.groupby(["Scene", "SPPBin"])["Count"]
        .sum()
        .reset_index(name="TotalCount")
    )

    merged = counts.merge(totals, on=["Scene", "SPPBin"], how="left")
    merged["Frequency"] = merged["Count"] / merged["TotalCount"]

    return merged


def smooth_frequencies(freq_df: pd.DataFrame, all_actions: list[int]) -> pd.DataFrame:
    out_rows = []

    for scene in sorted(freq_df["Scene"].unique()):
        scene_df = freq_df[freq_df["Scene"] == scene].copy()

        pivot = (
            scene_df.pivot_table(
                index="SPPBin",
                columns="Action",
                values="Frequency",
                fill_value=0.0,
            )
            .reindex(columns=all_actions, fill_value=0.0)
            .sort_index()
        )

        if SMOOTHING_WINDOW > 1:
            pivot = pivot.rolling(window=SMOOTHING_WINDOW, min_periods=1).mean()

        # Renormalize rows so they sum to 1 after smoothing
        row_sums = pivot.sum(axis=1).replace(0.0, 1.0)
        pivot = pivot.div(row_sums, axis=0)

        melted = (
            pivot.reset_index()
            .melt(id_vars="SPPBin", var_name="Action", value_name="Frequency")
        )
        melted["Scene"] = scene
        out_rows.append(melted)

    return pd.concat(out_rows, ignore_index=True)


def make_scene_matrix(scene_freq_df: pd.DataFrame, all_actions: list[int]):
    pivot = (
        scene_freq_df.pivot_table(
            index="SPPBin",
            columns="Action",
            values="Frequency",
            fill_value=0.0,
        )
        .reindex(columns=all_actions, fill_value=0.0)
        .sort_index()
    )

    x_values = pivot.index.to_list()
    matrix = pivot.to_numpy()

    return x_values, matrix


def compute_entropy(matrix: np.ndarray) -> np.ndarray:
    eps = 1e-12
    return -np.sum(matrix * np.log2(matrix + eps), axis=1)


# ============================================================
# PLOTTING
# ============================================================

def plot_stacked_area(scene_name: str, x_values, matrix, action_labels, out_path: str):
    plt.figure(figsize=(10, 6))
    plt.stackplot(x_values, matrix.T, labels=action_labels)
    plt.xscale("log", base=2)
    plt.xticks(x_values, [str(x) for x in x_values])
    plt.ylim(0.0, 1.0)
    plt.xlabel("SPP Bin")
    plt.ylabel("Action Frequency")
    plt.title(f"Action Distribution vs SPP - {scene_name}")
    plt.legend(loc="upper right")
    plt.tight_layout()
    plt.savefig(out_path, dpi=250, bbox_inches="tight")
    plt.show()
    plt.close()


def plot_lines(scene_name: str, x_values, matrix, action_labels, out_path: str):
    plt.figure(figsize=(10, 6))
    for i, label in enumerate(action_labels):
        plt.plot(x_values, matrix[:, i], marker="o", linewidth=2, label=label)

    plt.xscale("log", base=2)
    plt.xticks(x_values, [str(x) for x in x_values])
    plt.ylim(0.0, 1.0)
    plt.xlabel("SPP Bin")
    plt.ylabel("Action Frequency")
    plt.title(f"Action Frequency vs SPP - {scene_name}")
    plt.legend(loc="upper right")
    plt.tight_layout()
    plt.savefig(out_path, dpi=250, bbox_inches="tight")
    plt.show()
    plt.close()


def plot_heatmap(scene_name: str, x_values, matrix, action_labels, out_path: str):
    plt.figure(figsize=(10, 4))
    plt.imshow(matrix.T, aspect="auto", interpolation="nearest")
    plt.colorbar(label="Frequency")
    plt.xticks(range(len(x_values)), [str(x) for x in x_values])
    plt.yticks(range(len(action_labels)), action_labels)
    plt.xlabel("SPP Bin")
    plt.ylabel("Action")
    plt.title(f"Action Frequency Heatmap - {scene_name}")
    plt.tight_layout()
    plt.savefig(out_path, dpi=250, bbox_inches="tight")
    plt.show()
    plt.close()


def plot_entropy(scene_name: str, x_values, matrix, out_path: str):
    entropy = compute_entropy(matrix)

    plt.figure(figsize=(8, 5))
    plt.plot(x_values, entropy, marker="o", linewidth=2)
    plt.xscale("log", base=2)
    plt.xticks(x_values, [str(x) for x in x_values])
    plt.xlabel("SPP Bin")
    plt.ylabel("Action Entropy (bits)")
    plt.title(f"Policy Entropy vs SPP - {scene_name}")
    plt.tight_layout()
    plt.savefig(out_path, dpi=250, bbox_inches="tight")
    plt.show()
    plt.close()


def plot_state_action_heatmap(df: pd.DataFrame, scene_name: str, out_path: str, top_n_states: int = 20):
    scene_df = df[df["Scene"] == scene_name].copy()

    state_counts = scene_df["State"].value_counts().head(top_n_states).index.tolist()
    scene_df = scene_df[scene_df["State"].isin(state_counts)]

    if scene_df.empty:
        print(f"No state-action data available for scene: {scene_name}")
        return

    table = (
        scene_df.groupby(["State", "Action"])
        .size()
        .reset_index(name="Count")
        .pivot_table(index="State", columns="Action", values="Count", fill_value=0)
        .sort_index()
    )

    row_sums = table.sum(axis=1).replace(0, 1)
    norm_table = table.div(row_sums, axis=0)

    plt.figure(figsize=(8, max(5, len(norm_table) * 0.25)))
    plt.imshow(norm_table.to_numpy(), aspect="auto", interpolation="nearest")
    plt.colorbar(label="P(Action | State)")
    plt.xticks(range(len(norm_table.columns)), [f"A{c}" for c in norm_table.columns])
    plt.yticks(range(len(norm_table.index)), [str(i) for i in norm_table.index])
    plt.xlabel("Action")
    plt.ylabel("State")
    plt.title(f"State-Action Frequency Heatmap - {scene_name}")
    plt.tight_layout()
    plt.savefig(out_path, dpi=250, bbox_inches="tight")
    plt.show()
    plt.close()


def plot_average_across_scenes(freq_df: pd.DataFrame, all_actions: list[int], action_labels, output_dir: str):
    grouped = (
        freq_df.groupby(["SPPBin", "Action"])["Frequency"]
        .mean()
        .reset_index()
    )

    pivot = (
        grouped.pivot_table(
            index="SPPBin",
            columns="Action",
            values="Frequency",
            fill_value=0.0,
        )
        .reindex(columns=all_actions, fill_value=0.0)
        .sort_index()
    )

    x_values = pivot.index.to_list()
    matrix = pivot.to_numpy()

    plot_stacked_area(
        "Average Across Scenes",
        x_values,
        matrix,
        action_labels,
        os.path.join(output_dir, "average_stacked.png"),
    )

    plot_lines(
        "Average Across Scenes",
        x_values,
        matrix,
        action_labels,
        os.path.join(output_dir, "average_lines.png"),
    )

    plot_heatmap(
        "Average Across Scenes",
        x_values,
        matrix,
        action_labels,
        os.path.join(output_dir, "average_heatmap.png"),
    )

    plot_entropy(
        "Average Across Scenes",
        x_values,
        matrix,
        os.path.join(output_dir, "average_entropy.png"),
    )


# ============================================================
# MAIN
# ============================================================

def main():
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    df = load_data(CSV_PATH)

    if FILTER_USING_RL:
        if USING_RL_COLUMN not in df.columns:
            raise ValueError(
                f"FILTER_USING_RL=True but column '{USING_RL_COLUMN}' was not found in the CSV"
            )
        df = df[df[USING_RL_COLUMN] == USING_RL_VALUE].copy()

    if SCENES_TO_PLOT is not None:
        df = df[df["Scene"].isin(SCENES_TO_PLOT)].copy()

    if df.empty:
        raise ValueError("No rows left after filtering.")

    df = add_spp_bin(df)

    raw_freq_df = compute_action_frequencies(df)

    all_actions = sorted(df["Action"].dropna().unique().tolist())

    if ACTION_LABELS is not None:
        if len(ACTION_LABELS) != len(all_actions):
            raise ValueError(
                f"ACTION_LABELS has length {len(ACTION_LABELS)}, but CSV contains "
                f"{len(all_actions)} unique actions: {all_actions}"
            )
        action_labels = ACTION_LABELS
    else:
        action_labels = [f"Action {a}" for a in all_actions]

    freq_df = smooth_frequencies(raw_freq_df, all_actions)

    scenes = sorted(df["Scene"].unique().tolist())

    print("Scenes found:", scenes)
    print("Actions found:", all_actions)

    for scene_name in scenes:
        print(f"Processing scene: {scene_name}")

        scene_freq_df = freq_df[freq_df["Scene"] == scene_name]
        x_values, matrix = make_scene_matrix(scene_freq_df, all_actions)

        if len(x_values) == 0:
            print(f"Skipping {scene_name}: no data")
            continue

        scene_safe = safe_name(scene_name)

        plot_stacked_area(
            scene_name,
            x_values,
            matrix,
            action_labels,
            os.path.join(OUTPUT_DIR, f"{scene_safe}_stacked.png"),
        )

        plot_lines(
            scene_name,
            x_values,
            matrix,
            action_labels,
            os.path.join(OUTPUT_DIR, f"{scene_safe}_lines.png"),
        )

        plot_heatmap(
            scene_name,
            x_values,
            matrix,
            action_labels,
            os.path.join(OUTPUT_DIR, f"{scene_safe}_heatmap.png"),
        )

        plot_entropy(
            scene_name,
            x_values,
            matrix,
            os.path.join(OUTPUT_DIR, f"{scene_safe}_entropy.png"),
        )

        plot_state_action_heatmap(
            df,
            scene_name,
            os.path.join(OUTPUT_DIR, f"{scene_safe}_state_action_heatmap.png"),
            top_n_states=TOP_N_STATES,
        )

    plot_average_across_scenes(freq_df, all_actions, action_labels, OUTPUT_DIR)

    print(f"Done. Clean plots saved to: {OUTPUT_DIR}")


if __name__ == "__main__":
    main()
