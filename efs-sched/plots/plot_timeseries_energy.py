#!/usr/bin/env python3
import sys
import pandas as pd
import matplotlib.pyplot as plt

csv = sys.argv[1]
out = sys.argv[2]

df = pd.read_csv(csv)

pid_df = df[df["pid"] != 0].copy()
if pid_df.empty:
    raise RuntimeError("No per-PID workload rows")

pid_df = pid_df.sort_values("time_s")

plt.figure(figsize=(7, 4))

for (pid, comm), g in pid_df.groupby(["pid", "comm"]):
    plt.step(
        g["time_s"],
        g["pid_energy_j"],
        where="post",
        linewidth=2,
        label=f"{comm} ({pid})",
    )

plt.xlabel("Time (s)")
plt.ylabel("Cumulative Energy (J)")
plt.grid(True, linestyle=":", alpha=0.6)
plt.legend()
plt.tight_layout()
plt.savefig(out, bbox_inches="tight")
