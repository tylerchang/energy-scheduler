#!/usr/bin/env python3
import sys
import pandas as pd
import matplotlib.pyplot as plt

WINDOW_S = 0.1

csv = sys.argv[1]
out = sys.argv[2]

df = pd.read_csv(csv)
sys_df = df.groupby("time_s", as_index=False).first()

t = sys_df["time_s"].values
e = sys_df["sys_energy_j"].values

power_t = []
power = []

j = 0
for i in range(len(t)):
    while t[i] - t[j] > WINDOW_S:
        j += 1
    if i > j:
        dt = t[i] - t[j]
        de = e[i] - e[j]
        if dt > 0:
            power_t.append(t[i])
            power.append(de / dt)

plt.figure(figsize=(7, 4))
plt.plot(power_t, power, linewidth=2)

plt.xlabel("Time (s)")
plt.ylabel("Power (W)")
plt.grid(True, linestyle=":", alpha=0.6)
plt.tight_layout()
plt.savefig(out, bbox_inches="tight")
