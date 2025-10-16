#!/usr/bin/env python3
import time
import glob

HWMON_PATH = "/sys/class/hwmon/hwmon7"  # adjust if your amd_energy device is elsewhere
INTERVAL = 1.0  # seconds

# Discover energy input + label pairs
energy_files = sorted(glob.glob(f"{HWMON_PATH}/energy*_input"))
labels = [open(f.replace("_input", "_label")).read().strip() for f in energy_files]

def read_values():
    vals = []
    for f in energy_files:
        try:
            vals.append(int(open(f).read().strip()))
        except:
            vals.append(0)
    return vals

# Initial snapshot
prev_vals = read_values()
prev_time = time.time()

print(f"Monitoring {len(energy_files)} energy domains every {INTERVAL}s...\n")

while True:
    time.sleep(INTERVAL)
    now_vals = read_values()
    now_time = time.time()
    dt = now_time - prev_time

    print(f"--- {dt:.2f}s interval ---")
    for label, v_now, v_prev in zip(labels, now_vals, prev_vals):
        dE = v_now - v_prev
        if dE < 0:  # handle wraparound (32-bit counters)
            dE += (1 << 32)
        power_W = (dE / 1e6) / dt  # µJ → J, divide by seconds
        print(f"{label:10s}: {power_W:8.2f} W")
    print()

    prev_vals = now_vals
    prev_time = now_time

