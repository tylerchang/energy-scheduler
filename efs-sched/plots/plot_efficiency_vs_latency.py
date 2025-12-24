#!/usr/bin/env python3
import sys
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np

csv = sys.argv[1]
out = sys.argv[2]

df = pd.read_csv(csv)

# -------------------------------
# Separate rows
# -------------------------------

pid_df = df[df["pid"] != 0].copy()
sys_df = df[df["pid"] == 0].copy()

if pid_df.empty or sys_df.empty:
    raise RuntimeError("Missing PID or system rows")

# -------------------------------
# Experiment duration
# -------------------------------

duration_s = sys_df["time_s"].max() - sys_df["time_s"].min()
if duration_s <= 0:
    raise RuntimeError("Invalid experiment duration")

# -------------------------------
# Aggregate per-PID metrics
# -------------------------------

agg = (
    pid_df.groupby(["pid", "comm"], as_index=False)
          .agg(
              energy_j=("pid_energy_j", "max"),
              power=("pid_power", "median"),
          )
)

# Avoid zeros (log scale)
agg = agg[(agg["energy_j"] > 0) & (agg["power"] > 0)]

# -------------------------------
# Metrics
# -------------------------------

agg["energy_rate"] = agg["energy_j"] / duration_s

# Scheduler-delay proxy:
# lower power ≈ more waiting
agg["delay_proxy"] = 1.0 / agg["power"]

# -------------------------------
# Plot
# -------------------------------

plt.figure(figsize=(6, 5))
plt.scatter(
    agg["delay_proxy"],
    agg["energy_rate"],
    alpha=0.7,
)

plt.xscale("log")
plt.yscale("log")

plt.xlabel("Scheduling delay proxy (1 / power)")
plt.ylabel("Average energy rate (J/s)")

plt.grid(True, which="both", linestyle=":", alpha=0.6)
plt.tight_layout()
plt.savefig(out, bbox_inches="tight")
