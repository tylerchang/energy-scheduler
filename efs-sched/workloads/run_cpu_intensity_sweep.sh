#!/usr/bin/env bash
# associated with plot_mem_intensity_comparison.py
set -e

DUR=20
CPU_INTENSITIES=(4 8 16 32)
mkdir -p runs

for ws in "${CPU_INTENSITIES[@]}"; do
    echo "Running mem_miss working_set=${ws}MB"

    OUT="runs/cpu_ws_${ws}.csv"

    ./poll_energy_extended 20 workload=stress > "$OUT" &
    LOGGER_PID=$!

    stress-ng --cpu "$ws" -t 20s

    kill $LOGGER_PID
    wait || true
done
