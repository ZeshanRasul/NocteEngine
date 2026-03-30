# -*- coding: utf-8 -*-
"""
Created on Mon Mar 30 01:12:34 2026

@author: Z
"""

import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

rl = pd.read_csv("run_7/RL_Metrics.csv")
baseline = pd.read_csv("run_7/Baseline_Metrics.csv")

plt.plot(rl["Iteration"], rl["Luminance Variance"], label="RL")
plt.plot(baseline["Iteration"], baseline["Luminance Variance"], label="Balanced")
running_avg = np.convolve((baseline["Luminance Variance"], rl["Luminance Variance"]), np.ones(100)/100, mode='valid')
print(running_avg)
plt.xlabel("Iteration (SPP)")
plt.ylabel("Variance")
plt.title("Variance vs Iteration")
plt.legend()
plt.grid()

plt.show()