#!/usr/bin/env bash
# associated with plot_mem_intensity_comparison.py
set -e

DUR=20
INTENSITIES=(8 64 256 1024)
mkdir -p runs

for ws in "${INTENSITIES[@]}"; do
    echo "Running mem_miss working_set=${ws}MB"

    OUT="runs/mem_ws_${ws}.csv"

    ./poll_energy_extended 20 workload=mem > "$OUT" &
    LOGGER_PID=$!

    ../mem_miss #1024 "$ws" 0 1 4096 0 "$DUR"

    kill $LOGGER_PID
    wait || true
done
