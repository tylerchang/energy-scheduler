#!/usr/bin/env bash
# Sequential mixed workload:
#   Phase 1: CPU-only
#   Phase 2: Memory-only
#   Phase 3: CPU + Memory
set -e

CPU_CPUS=16
MEM_WS_MB=512
PHASE_DUR=7

OUT="runs/mixed_sequential.csv"
mkdir -p runs

echo "Running sequential mixed workload"
echo "  CPU_CPUS=${CPU_CPUS}"
echo "  MEM_WS_MB=${MEM_WS_MB}"
echo "  PHASE_DUR=${PHASE_DUR}s"

# Start logger (capture both workloads)
../poll_energy_extended 20 workload=mem,stress > "$OUT" &
LOGGER_PID=$!

# ---------------- Phase 1: CPU-only ----------------
echo "Phase 1: CPU-only"
stress-ng --cpu "${CPU_CPUS}" -t "${PHASE_DUR}s"

# ---------------- Phase 2: Memory-only ----------------
echo "Phase 2: Memory-only"
../mem_miss 1024 "${MEM_WS_MB}" 0 1 4096 0 "${PHASE_DUR}"

# ---------------- Phase 3: Mixed ----------------
echo "Phase 3: Mixed CPU + Memory"
stress-ng --cpu "${CPU_CPUS}" -t "${PHASE_DUR}s" &
CPU_PID=$!

../mem_miss 1024 "${MEM_WS_MB}" 0 1 4096 0 "${PHASE_DUR}"

wait $CPU_PID || true

# Stop logger
kill $LOGGER_PID
wait || true

echo "Saved ${OUT}"
