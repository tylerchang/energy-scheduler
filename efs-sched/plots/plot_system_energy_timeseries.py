#!/usr/bin/env python3
import sys
import pandas as pd
import matplotlib.pyplot as plt

csv = sys.argv[1]
out = sys.argv[2]

df = pd.read_csv(csv)

sys_df = df[df["pid"] == 0].copy()
if sys_df.empty:
    raise RuntimeError("No system rows found")

sys_df = sys_df.sort_values("time_s")

plt.figure(figsize=(7, 4))

plt.step(
    sys_df["time_s"],
    sys_df["sys_energy_j"],
    where="post",
    linewidth=2,
    label="Core (task-attributed) energy",
)

plt.step(
    sys_df["time_s"],
    sys_df["sys_uncore_energy_j"],
    where="post",
    linewidth=2,
    label="Uncore / system energy",
)

plt.xlabel("Time (s)")
plt.ylabel("Energy (J)")
plt.grid(True, linestyle=":", alpha=0.6)
plt.legend()
plt.tight_layout()
plt.savefig(out, bbox_inches="tight")
