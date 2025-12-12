#!/bin/bash

# Run multiple mem_miss instances in parallel with different parameters
# but approximately the same runtime (via target_seconds).
#
# Usage:
#   ./run_mem_miss_mix.sh [path_to_mem_miss] [duration_seconds]
#
# If not given:
#   path_to_mem_miss defaults to ./mem_miss
#   duration_seconds defaults to 60

MEM_MISS="${1:-./mem_miss}"
DURATION="${2:-0}"   # target_seconds for each workload

if [ ! -x "$MEM_MISS" ]; then
    echo "Error: '$MEM_MISS' is not executable. Pass the correct path as the first argument."
    echo "Example: ./run_mem_miss_mix.sh /path/to/mem_miss 60"
    exit 1
fi

if ! [[ "$DURATION" =~ ^[0-9]+$ ]]; then
    echo "Error: duration_seconds must be an integer number of seconds"
    exit 1
fi

echo "Using mem_miss binary : $MEM_MISS"
echo "Target duration       : ${DURATION}s for each workload"
echo

# mem_miss usage:
#   mem_miss total_mb working_set_mb accesses pattern stride_bytes write_percent target_seconds
#
# We set accesses=0 for all and rely on target_seconds to fix runtime.
# This preserves the "intensity knobs": memory size, pattern, write ratio, etc.

declare -a NAMES
declare -a ARGS

# 1) Light: smaller working set, sequential-ish, mostly reads
NAMES+=("mem_light")
ARGS+=("512 256 0 0 4096 10 ${DURATION}")

# 2) Medium: mid-size working set, random pattern, balanced R/W
NAMES+=("mem_medium")
ARGS+=("1024 1024 0 1 4096 50 ${DURATION}")

# 3) Heavy: large working set, random, write-heavy
NAMES+=("mem_heavy")
ARGS+=("4096 4096 0 1 4096 90 ${DURATION}")

# 4) Very write-heavy but smaller than mem_heavy, also time-based
# NAMES+=("mem_timewriter")
# ARGS+=("2048 2048 0 1 4096 100 60")

PIDS=()

cleanup() {
    echo
    echo "Caught interrupt — killing all spawned mem_miss processes..."
    for pid in "${PIDS[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then
            echo "  Killing PID $pid"
            kill "$pid" 2>/dev/null
        fi
    done
    echo "Done."
    exit 0
}

trap cleanup SIGINT SIGTERM

echo "Launching ${#NAMES[@]} mem_miss workloads..."
echo

for i in "${!NAMES[@]}"; do
    name="${NAMES[$i]}"
    args="${ARGS[$i]}"

    echo "  Starting $name : $MEM_MISS $args"
    # exec -a sets argv[0], so /proc/<pid>/comm sees 'name'
    for i in {1..16..1}
    do 
        exec -a "$name" "$MEM_MISS" $args &
    pid=$!
    PIDS+=("$pid")
    done
    
done

echo
echo "All mem_miss instances launched:"
for i in "${!NAMES[@]}"; do
    echo "  ${NAMES[$i]} (PID ${PIDS[$i]})"
done
echo
echo "Press Ctrl+C to terminate them all early."

wait
