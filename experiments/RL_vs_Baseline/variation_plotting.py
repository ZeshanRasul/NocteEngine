# -*- coding: utf-8 -*-
"""
Created on Mon Mar 30 01:12:34 2026

@author: Z
"""

import pandas as pd
import matplotlib.pyplot as plt

rl = pd.read_csv("run_5/RL_Metrics.csv")
baseline = pd.read_csv("run_5/Baseline_Metrics.csv")

plt.plot(rl["Iteration"], rl["Luminance Variance"], label="RL")
plt.plot(baseline["Iteration"], baseline["Luminance Variance"], label="Balanced")

plt.xlabel("Iteration (SPP)")
plt.ylabel("Variance")
plt.title("Variance vs Iteration")
plt.legend()
plt.grid()

plt.show()