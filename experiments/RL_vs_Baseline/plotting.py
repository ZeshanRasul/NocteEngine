# -*- coding: utf-8 -*-
"""
Created on Mon Mar 30 05:01:30 2026

@author: Z
"""

import pandas as pd
import matplotlib.pyplot as plt

# -------- CONFIG --------
RL_PATH = "run_19/RL_Metrics.csv"
BASELINE_PATH = "run_19/Baseline_Metrics.csv"
WINDOW = 100  # smoothing window
# -----------------------

def load_data(path):
    df = pd.read_csv(path)

    # Ensure numeric columns
    df["Iteration"] = pd.to_numeric(df["Iteration"], errors="coerce")
    df["Luminance Variance"] = pd.to_numeric(df["Luminance Variance"], errors="coerce")
    df["Log Luminance Variance"] = pd.to_numeric(df["Log Luminance Variance"], errors="coerce")

    df = df.dropna(subset=["Iteration", "Luminance Variance", "Log Luminance Variance"])

    return df


def compute_metrics(df):
    df["var_ma"] = df["Luminance Variance"].rolling(WINDOW, min_periods=1).mean()
    df["var_cummean"] = df["Luminance Variance"].expanding().mean()

    df["logvar_ma"] = df["Log Luminance Variance"].rolling(WINDOW, min_periods=1).mean()
    df["logvar_cummean"] = df["Log Luminance Variance"].expanding().mean()

    return df


def plot_raw_variance(rl, baseline):
    plt.figure()
    plt.plot(rl["Iteration"], rl["Luminance Variance"], label="RL")
    plt.plot(baseline["Iteration"], baseline["Luminance Variance"], label="Balanced")

    plt.xlabel("Iteration (SPP)")
    plt.ylabel("Variance")
    plt.title("Raw Variance vs Iteration")
    plt.legend()
    plt.grid(True)

    plt.show()


def plot_smoothed_variance(rl, baseline):
    plt.figure()
    plt.plot(rl["Iteration"], rl["var_ma"], label="RL")
    plt.plot(baseline["Iteration"], baseline["var_ma"], label="Balanced")

    plt.xlabel("Iteration (SPP)")
    plt.ylabel("Variance (Moving Average)")
    plt.title(f"Smoothed Variance (Window={WINDOW})")
    plt.legend()
    plt.grid(True)

    plt.show()


def plot_cumulative_variance(rl, baseline):
    plt.figure()
    plt.plot(rl["Iteration"], rl["var_cummean"], label="RL")
    plt.plot(baseline["Iteration"], baseline["var_cummean"], label="Balanced")

    plt.xlabel("Iteration (SPP)")
    plt.ylabel("Cumulative Mean Variance")
    plt.title("Cumulative Mean Variance")
    plt.legend()
    plt.grid(True)

    plt.show()


def plot_log_variance(rl, baseline):
    plt.figure()
    plt.plot(rl["Iteration"], rl["logvar_ma"], label="RL")
    plt.plot(baseline["Iteration"], baseline["logvar_ma"], label="Balanced")

    plt.xlabel("Iteration (SPP)")
    plt.ylabel("Log Variance (Moving Average)")
    plt.title("Smoothed Log Variance")
    plt.legend()
    plt.grid(True)

    plt.show()


def plot_action_distribution(rl):
    counts = rl["Action"].value_counts()

    plt.figure()
    counts.plot(kind="bar")

    plt.xlabel("Action")
    plt.ylabel("Count")
    plt.title("RL Action Distribution")
    plt.grid(True)

    plt.show()


def print_summary(rl, baseline):
    print("\n--- SUMMARY ---")

    print("RL mean variance:", rl["Luminance Variance"].mean())
    print("Baseline mean variance:", baseline["Luminance Variance"].mean())

    print("RL final MA:", rl["var_ma"].iloc[-1])
    print("Baseline final MA:", baseline["var_ma"].iloc[-1])

    print("RL mean log variance:", rl["Log Luminance Variance"].mean())
    print("Baseline mean log variance:", baseline["Log Luminance Variance"].mean())


def main():
    rl = load_data(RL_PATH)
    baseline = load_data(BASELINE_PATH)

    rl = compute_metrics(rl)
    baseline = compute_metrics(baseline)

    # Plots
    plot_raw_variance(rl, baseline)
    plot_smoothed_variance(rl, baseline)
    plot_cumulative_variance(rl, baseline)
    plot_log_variance(rl, baseline)
    plot_action_distribution(rl)

    # Summary stats
    print_summary(rl, baseline)


if __name__ == "__main__":
    main()