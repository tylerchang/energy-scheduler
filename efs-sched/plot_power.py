#!/usr/bin/env python3
import re
import sys
import matplotlib.pyplot as plt

def parse_log(path):
    """
    Parse poller log and return:
      cpu_times_ns: list of cpu_time values (ns) from BPF total map (or None)
      wall_times_s: list of "Real time since poller start" values (seconds)
      powers:       list of PID power values (EMA), with 0 when map is empty

    Each '=== PID → power (EMA) ... ===' block is treated as one sample.
    If that block has no numeric rows, we record power=0 for that sample.
    """

    cpu_times_ns = []
    wall_times_s = []
    powers = []

    cur_cpu = None
    cur_wall = None

    # pending_sample tracks the sample started by a "PID → power" header
    pending_sample = None  # {"wall": float|None, "cpu": int|None, "have_value": bool}

    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        for raw_line in f:
            # strip ANSI clear-screen sequences
            line = raw_line.replace("\x1b[H\x1b[J", "")
            line = line.rstrip("\n")

            # Match wall time line:
            # "Real time since poller start: 93.021584 s"
            m_wall = re.search(r"Real time since poller start:\s+([\d\.]+)\s*s", line)
            if m_wall:
                cur_wall = float(m_wall.group(1))
                continue

            # Match cpu_time line (only present if you print 'total' map)
            # "  cpu_time:  131402767668 ns"
            m_cpu = re.search(r"cpu_time:\s+(\d+)\s+ns", line)
            if m_cpu:
                cur_cpu = int(m_cpu.group(1))
                continue

            # Detect start of PID power section:
            # "=== PID → power (EMA) (pid_to_power) [...] ==="
            if "PID → power (EMA)" in line:
                # If we had a previous pending sample with no value, treat it as 0
                if pending_sample is not None and not pending_sample["have_value"]:
                    powers.append(0)
                    wall_times_s.append(pending_sample["wall"])
                    cpu_times_ns.append(pending_sample["cpu"])
                # Start a new pending sample using the *current* wall/cpu time
                pending_sample = {"wall": cur_wall, "cpu": cur_cpu, "have_value": False}
                continue

            # Header line "PID      power"
            if pending_sample is not None and line.strip().startswith("PID") and "power" in line:
                # Just the table header, skip
                continue

            # Possible data row with "PID  power" numbers
            if pending_sample is not None and line.strip():
                parts = line.split()
                # e.g., "138316   39"
                if len(parts) >= 2 and parts[0].isdigit() and parts[1].isdigit():
                    power = int(parts[1])
                    powers.append(power)
                    wall_times_s.append(pending_sample["wall"])
                    cpu_times_ns.append(pending_sample["cpu"])
                    pending_sample["have_value"] = True
                    # We only care about the first PID row in that section
                    pending_sample = None
                continue

        # End of file: if there's a pending sample with no data, treat it as 0
        if pending_sample is not None and not pending_sample["have_value"]:
            powers.append(0)
            wall_times_s.append(pending_sample["wall"])
            cpu_times_ns.append(pending_sample["cpu"])

    return cpu_times_ns, wall_times_s, powers


def rolling_mean(values, window):
    if window <= 1 or window > len(values):
        return values
    out = []
    s = 0.0
    for i, v in enumerate(values):
        s += v
        if i >= window:
            s -= values[i - window]
        if i >= window - 1:
            out.append(s / window)
        else:
            out.append(values[i])  # before full window, just use raw
    return out


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} out.log [output.png] [--wall]")
        sys.exit(1)

    log_path = sys.argv[1]
    out_path = None
    use_wall = False

    # Parse optional args
    for arg in sys.argv[2:]:
        if arg == "--wall":
            use_wall = True
        elif not arg.startswith("-") and out_path is None:
            out_path = arg
        else:
            print(f"Warning: unknown or extra arg '{arg}', ignoring.")

    cpu_times_ns, wall_times_s, powers = parse_log(log_path)

    if not powers:
        print("No samples found in log (no 'PID → power (EMA)' blocks).")
        sys.exit(1)

    # Choose x-axis: prefer wall-time if requested, **or** if no cpu_time info
    have_wall = any(w is not None for w in wall_times_s)
    have_cpu = bool(cpu_times_ns) and any(c is not None for c in cpu_times_ns)

    if (use_wall and have_wall) or not have_cpu:
        # Use wall time as-is, but normalize so it starts at 0
        first_valid_idx = next(
            (i for i, w in enumerate(wall_times_s) if w is not None),
            0
        )
        t0 = wall_times_s[first_valid_idx]
        times = [(w - t0) if w is not None else 0.0 for w in wall_times_s]
        x_label = "Wall time since poller start (s)"
    else:
        # Use CPU time from total map
        # If some entries are None, fall back to the previous non-None value
        filled_cpu = []
        last = cpu_times_ns[0]
        if last is None:
            # find first non-None
            for c in cpu_times_ns:
                if c is not None:
                    last = c
                    break
        for c in cpu_times_ns:
            if c is not None:
                last = c
            filled_cpu.append(last)
        t0 = filled_cpu[0]
        times = [(t - t0) / 1e9 for t in filled_cpu]
        x_label = "CPU time (s, from BPF total.cpu_time)"

    # Smoothing window ~ 30 points across the plot
    smooth_window = max(1, len(powers) // 30)
    smoothed = rolling_mean(powers, smooth_window)

    plt.figure(figsize=(10, 6))
    plt.plot(times, powers, linewidth=0.7, label="Power (raw)")
    if smooth_window > 1:
        plt.plot(times, smoothed, linewidth=2.0,
                 label=f"Rolling mean (window={smooth_window})")

    # Red vertical dashed lines every 5 seconds
    max_t = max(times)
    spacing = 5.0  # seconds
    t = spacing
    while t <= max_t:
        plt.axvline(x=t, linestyle="--", color="red", linewidth=0.8)
        t += spacing

    plt.xlabel(x_label)
    plt.ylabel("Power (EMA)")
    plt.title("PID Power vs Time from poller log")
    plt.grid(True)
    plt.legend()
    plt.tight_layout()

    if out_path:
        plt.savefig(out_path, dpi=150, bbox_inches="tight")
        print(f"Saved plot to {out_path}")
    else:
        plt.show()


if __name__ == "__main__":
    main()
