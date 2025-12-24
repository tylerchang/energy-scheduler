#!/usr/bin/env python3
import sys
import pandas as pd
import matplotlib.pyplot as plt

csv = sys.argv[1]
out = sys.argv[2]

df = pd.read_csv(csv)

# -------------------------------
# Separate system and PID rows
# -------------------------------

pid_df = df[df["pid"] != 0].copy()
sys_df = df[df["pid"] == 0].copy()

if pid_df.empty:
    raise RuntimeError("No per-PID workload rows found in CSV")

# -------------------------------
# Compute experiment duration
# -------------------------------

t_start = sys_df["time_s"].min()
t_end   = sys_df["time_s"].max()
duration_s = t_end - t_start

if duration_s <= 0:
    raise RuntimeError("Invalid experiment duration")

# -------------------------------
# Aggregate per-PID energy
# -------------------------------

agg = (
    pid_df.groupby(["pid", "comm"], as_index=False)
          .agg(pid_energy_j=("pid_energy_j", "max"))
)

# -------------------------------
# Compute average power (J/s)
# -------------------------------

agg["energy_per_s"] = agg["pid_energy_j"] / duration_s

agg = agg.sort_values("energy_per_s")

# -------------------------------
# Plot
# -------------------------------

plt.figure(figsize=(8, 4))
plt.bar(agg["comm"], agg["energy_per_s"])

plt.yscale("log")
plt.ylabel("Average Energy Rate (J/s, log scale)")
plt.xlabel("Process")

plt.xticks(rotation=45, ha="right")
plt.grid(True, axis="y", linestyle=":", alpha=0.6)
plt.tight_layout()
plt.savefig(out, bbox_inches="tight")
