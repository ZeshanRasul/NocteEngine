import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

rl = pd.read_csv("run_23/RL_Metrics.csv")
base = pd.read_csv("run_23/Baseline_Metrics.csv")

x = rl["Iteration"].to_numpy()

rl_var = rl["Luminance Variance"].to_numpy()
base_var = base["Luminance Variance"].to_numpy()

rl_log = rl["Log Luminance Variance"].to_numpy()
base_log = base["Log Luminance Variance"].to_numpy()

window = 100

def moving_average(y, w):
    return pd.Series(y).rolling(w, min_periods=1).mean().to_numpy()

def cumulative_mean(y):
    return np.cumsum(y) / np.arange(1, len(y) + 1)

# Core derived series
var_diff = rl_var - base_var
log_diff = rl_log - base_log

var_pct_improve = 100.0 * (base_var - rl_var) / base_var
log_pct_improve = 100.0 * (base_log - rl_log) / base_log

cum_rl_var = cumulative_mean(rl_var)
cum_base_var = cumulative_mean(base_var)
cum_diff_var = cum_rl_var - cum_base_var

sm_rl_var = moving_average(rl_var, window)
sm_base_var = moving_average(base_var, window)
sm_diff_var = sm_rl_var - sm_base_var

sm_rl_log = moving_average(rl_log, window)
sm_base_log = moving_average(base_log, window)
sm_diff_log = sm_rl_log - sm_base_log

# ---------- Plot 1: Smoothed variance difference ----------
plt.figure(figsize=(8, 4.8))
plt.plot(x, sm_diff_var, label="Smoothed Variance Difference (RL - Baseline)")
plt.axhline(0.0, linestyle="--")
plt.title(f"Smoothed Variance Difference (Window={window})")
plt.xlabel("Iteration (SPP)")
plt.ylabel("Variance Difference")
plt.legend()
plt.tight_layout()
plt.show()

# ---------- Plot 2: Cumulative mean variance difference ----------
plt.figure(figsize=(8, 4.8))
plt.plot(x, cum_diff_var, label="Cumulative Mean Difference (RL - Baseline)")
plt.axhline(0.0, linestyle="--")
plt.title("Cumulative Mean Variance Difference")
plt.xlabel("Iteration (SPP)")
plt.ylabel("Cumulative Mean Difference")
plt.legend()
plt.tight_layout()
plt.show()

# ---------- Plot 3: Smoothed percent improvement ----------
plt.figure(figsize=(8, 4.8))
plt.plot(x, moving_average(var_pct_improve, window), label="Smoothed % Improvement")
plt.axhline(0.0, linestyle="--")
plt.title(f"Smoothed Variance % Improvement (Window={window})")
plt.xlabel("Iteration (SPP)")
plt.ylabel("% Improvement of RL over Baseline")
plt.legend()
plt.tight_layout()
plt.show()

# ---------- Plot 4: Final summary bar chart ----------
summary_labels = [
    "Mean Variance",
    "Final CumMean Variance",
    "Mean LogVar",
    "Final CumMean LogVar"
]

mean_var_rl = rl_var.mean()
mean_var_base = base_var.mean()
final_cum_var_rl = cum_rl_var[-1]
final_cum_var_base = cum_base_var[-1]

cum_rl_log = cumulative_mean(rl_log)
cum_base_log = cumulative_mean(base_log)

mean_log_rl = rl_log.mean()
mean_log_base = base_log.mean()
final_cum_log_rl = cum_rl_log[-1]
final_cum_log_base = cum_base_log[-1]

rl_vals = [mean_var_rl, final_cum_var_rl, mean_log_rl, final_cum_log_rl]
base_vals = [mean_var_base, final_cum_var_base, mean_log_base, final_cum_log_base]

idx = np.arange(len(summary_labels))
width = 0.35

plt.figure(figsize=(9, 4.8))
plt.bar(idx - width / 2, rl_vals, width, label="RL")
plt.bar(idx + width / 2, base_vals, width, label="Baseline")
plt.xticks(idx, summary_labels, rotation=15)
plt.ylabel("Value")
plt.title("Summary Metrics")
plt.legend()
plt.tight_layout()
plt.show()

# ---------- Printed summary ----------
print("=== Summary ===")
print(f"Mean variance RL:        {mean_var_rl:.6f}")
print(f"Mean variance Baseline:  {mean_var_base:.6f}")
print(f"Mean variance diff:      {mean_var_rl - mean_var_base:.6f}")
print(f"Mean variance % improve: {100.0 * (mean_var_base - mean_var_rl) / mean_var_base:.6f}%")
print()

print(f"Final cummean RL:        {final_cum_var_rl:.6f}")
print(f"Final cummean Baseline:  {final_cum_var_base:.6f}")
print(f"Final cummean diff:      {final_cum_var_rl - final_cum_var_base:.6f}")
print(f"Final cummean % improve: {100.0 * (final_cum_var_base - final_cum_var_rl) / final_cum_var_base:.6f}%")
print()

print(f"Mean logvar RL:          {mean_log_rl:.6f}")
print(f"Mean logvar Baseline:    {mean_log_base:.6f}")
print(f"Mean logvar diff:        {mean_log_rl - mean_log_base:.6f}")
print(f"Mean logvar % improve:   {100.0 * (mean_log_base - mean_log_rl) / mean_log_base:.6f}%")