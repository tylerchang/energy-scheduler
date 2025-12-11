#!/bin/bash

# Usage:
#   ./workload.sh <program_base> <count> [args...]
#
# Example:
#   ./workload.sh ./stress_core 10

if [ $# -lt 2 ]; then
    echo "Usage: $0 <program_base> <count> [args...]"
    exit 1
fi

BASE="$1"
COUNT="$2"
shift 2   # shift off program_base and count

if ! [[ "$COUNT" =~ ^[0-9]+$ ]]; then
    echo "COUNT must be a number"
    exit 1
fi

PIDS=()

cleanup() {
    echo ""
    echo "Caught interrupt — killing all spawned processes..."
    for pid in "${PIDS[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then
            echo "  Killing PID $pid"
            kill "$pid" 2>/dev/null
        fi
    done
    echo "Done."
    exit 0
}

# Catch Ctrl+C (SIGINT) and termination (SIGTERM)
trap cleanup SIGINT SIGTERM

echo "Launching $COUNT instances of $BASE ..."

for ((i=1; i<=COUNT; i++)); do
    NAME="${BASE}${i}"
    echo "  Starting $NAME"

    # exec -a changes process name as seen in /proc/<pid>/comm
    exec -a "$NAME" "$BASE" "$@" &
    PID=$!
    PIDS+=("$PID")
done

echo "All processes launched."
echo "Press Ctrl+C to terminate them."

# Wait for children; if any exits naturally, continue waiting
wait
